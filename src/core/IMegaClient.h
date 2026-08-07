#pragma once
#include "AccountInfo.h"
#include "DownloadOutcome.h"
#include "FileEntry.h"
#include "NodeInfo.h"
#include "PathSegment.h"
#include "Result.h"
#include "SortOrder.h"
#include "UploadOutcome.h"
#include "UserAttribute.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// How the callback-shaped methods below actually deliver, which is three ways,
// not one. MegaSdkClient is the implementation this describes; a caller that
// only handles the first mode is wrong on the other two.
//
//   1. Truly asynchronous, on an SDK-internal thread. The listener-backed
//      methods: login/fetchNodes/download/upload/getThumbnail and every
//      mutating call. This is the mode the rest of this file's comments mean
//      by "background thread".
//   2. Always synchronous, on the calling thread -- onDone has already run by
//      the time the method returns. getRootChildren, getChildren, search,
//      getPath and getNodeInfo, all of which are in-memory reads of the node
//      tree the SDK holds after fetchNodes. FolderNavigationService's
//      lock-free design rests on this; see its own header comment.
//   3. Synchronous *on failure only*, on the calling thread. Every method that
//      resolves a handle first fails in-stack when that handle resolves to
//      nothing -- logged out, fetchNodes not run yet, or the node deleted from
//      another client -- and only reaches mode 1 once the handle is good. This
//      is the mode that is easy to miss, because the happy path looks purely
//      asynchronous.
//
// Consequence for callers, and the reason mode 3 is spelled out here: onDone
// may run inside your own call, before the method returns. Do not hold a lock
// across the call, and if onDone re-enters the code that issued the call --
// a queue that auto-starts its next job, say -- guard against recursing once
// per pending item. DownloadService/UploadService/ThumbnailService each carry
// an explicit trampoline for exactly this.
class IMegaClient
{
public:
    virtual ~IMegaClient() = default;

    virtual void login(const std::string& email,
                       const std::string& password,
                       std::function<void(Result<void>)> onDone) = 0;

    // MegaApi::fastLogin equivalent: re-authenticates using a session token
    // previously obtained from currentSessionToken(), skipping password
    // verification. Used on startup to restore a persisted session.
    virtual void loginWithSession(const std::string& sessionToken,
                                  std::function<void(Result<void>)> onDone) = 0;

    // For accounts with two-factor auth enabled: called after login() has
    // already failed with MegaErrorCode::kEMfaRequired, resubmitting the same
    // email/password alongside the 6-digit pin.
    virtual void multiFactorAuthLogin(const std::string& email,
                                      const std::string& password,
                                      const std::string& pin,
                                      std::function<void(Result<void>)> onDone) = 0;

    virtual void logout(std::function<void(Result<void>)> onDone) = 0;

    // MegaApi::dumpSession equivalent. Synchronous, unlike every other method
    // here -- it's a local read of already-held session state, no network
    // round-trip. Fails if not currently logged in.
    virtual Result<std::string> currentSessionToken() const = 0;

    // MegaApi::getMyUserHandleBinary equivalent. Synchronous, same rationale as
    // currentSessionToken(). Fails if not currently logged in. Used to scope
    // per-account persisted state (quick-access pins) without account identity
    // leaking into ISessionStore.
    virtual Result<std::uint64_t> currentUserHandle() const = 0;

    // Must be called after a successful login(), before getRootChildren().
    //
    // Takes the same two-callback shape as download()/upload() below, for the
    // same reason in reverse: MegaRequestListener::onRequestUpdate exists for
    // exactly one request type, TYPE_FETCH_NODES (megaapi.h:9261).
    //
    // What onProgress reports is narrower than it looks, and callers must
    // treat it as such:
    //   - It is the HTTP download progress of the `f` API response body, not
    //     "nodes processed". The decrypt/tree-build/DB-write work that
    //     follows the download has no progress signal of any kind, and on a
    //     640k-node account it was 57% of the total wall time (162s download
    //     vs. 218s afterwards). Do not present the bytes as overall progress.
    //   - It may fire zero times. When the SDK's local state-cache DB is
    //     valid, fetchNodes reads from it instead of hitting the network and
    //     no progress event is ever emitted (measured: 0 events, 619ms).
    //   - The final event is not guaranteed to be exactly 100%; the last one
    //     observed in practice was 99.44%.
    // Full measurements: docs/investigations/FETCHNODES_PROGRESS_INVESTIGATION.md.
    virtual void fetchNodes(
        std::function<void(std::uint64_t transferredBytes, std::uint64_t totalBytes)> onProgress,
        std::function<void(Result<void>)> onDone) = 0;

    // Asks the API for action packets not yet applied, so the node tree the
    // getters below read is current as of this call. Unlike those getters this
    // is a genuine server round-trip (MegaApi::catchup) -- it is what makes a
    // user-initiated refresh mean anything, since the getters alone only ever
    // re-read what the SDK happens to have been told already.
    virtual void syncPendingChanges(std::function<void(Result<void>)> onDone) = 0;

    // Must be called after a successful fetchNodes(). Synchronous under the
    // hood, but kept callback-shaped for interface consistency. order is
    // forwarded to MegaApi::getChildren's own order argument (server-side
    // sort, see SortOrder.h).
    virtual void getRootChildren(SortOrder order,
                                 std::function<void(Result<std::vector<FileEntry>>)> onDone) = 0;

    // Must be called after a successful fetchNodes(). Synchronous under the
    // hood, but kept callback-shaped for interface consistency. order is
    // forwarded to MegaApi::getChildren's own order argument (server-side
    // sort, see SortOrder.h).
    virtual void getChildren(std::uint64_t handle,
                             SortOrder order,
                             std::function<void(Result<std::vector<FileEntry>>)> onDone) = 0;

    // Recursive name search rooted at ancestorHandle (ignored when isRoot is
    // true, same isRoot-sentinel convention as FolderNavigationService's
    // Location). Must be called after a successful fetchNodes(). Synchronous
    // under the hood (MegaApi::search()), but kept callback-shaped for
    // interface consistency. order is forwarded to MegaApi::search's own
    // order argument (server-side sort, see SortOrder.h).
    virtual void search(std::uint64_t ancestorHandle,
                        bool isRoot,
                        const std::string& query,
                        SortOrder order,
                        std::function<void(Result<std::vector<FileEntry>>)> onDone) = 0;

    // Downloads the file identified by handle to the exact local file path
    // destinationPath. destinationPath is already fully resolved by the
    // caller (e.g. DownloadController) -- IMegaClient has no filesystem
    // access of its own, same division of responsibility expected from
    // IFileSystem later.
    //
    // Diverges from the Result<T>-in-one-callback shape used by every
    // method above: MegaApi::startDownload is MegaTransferListener-based,
    // not MegaRequestListener-based, reporting progress and completion via
    // two separate callbacks. onProgress may fire zero or more times with
    // (transferredBytes, totalBytes) before the transfer finishes; onDone
    // fires exactly once, terminally, carrying a DownloadOutcome whose
    // localPath is the *actual* final local path (MegaTransfer::getPath()),
    // which can differ from destinationPath if a name collision caused the
    // SDK to rename the saved file -- and whose alreadyPresent flag
    // distinguishes that rename case from the other collision outcome (an
    // identical file already at destinationPath, which the SDK detects via
    // fingerprint and skips re-downloading entirely). Same background-thread
    // caveat as the rest of this file applies to both callbacks -- except that
    // an unresolvable handle fails in delivery mode 3, running onDone on the
    // calling thread before this call returns.
    virtual void download(
        std::uint64_t handle,
        const std::string& destinationPath,
        std::function<void(std::uint64_t transferredBytes, std::uint64_t totalBytes)> onProgress,
        std::function<void(Result<DownloadOutcome>)> onDone) = 0;

    // Uploads the local *file* at localPath into the folder identified by
    // parentHandle (parentIsRoot makes parentHandle meaningless, same
    // sentinel convention as getChildren/getPath above). localPath is
    // already fully resolved by the caller -- absolute, with native
    // separators -- exactly like download()'s destinationPath, since
    // IMegaClient has no filesystem access of its own.
    //
    // Files only: MegaApi::startUpload also accepts a directory (uploading
    // it recursively), but this app never exposes that, so callers must
    // filter directories out beforehand.
    //
    // Same two-callback shape as download() and for the same reason
    // (MegaTransferListener, not MegaRequestListener): onProgress may fire
    // zero or more times with (transferredBytes, totalBytes), onDone fires
    // exactly once, terminally. Also like download(), a parentHandle that no
    // longer resolves fails in delivery mode 3 -- onDone on the calling
    // thread, before this call returns.
    virtual void
    upload(const std::string& localPath,
           std::uint64_t parentHandle,
           bool parentIsRoot,
           std::function<void(std::uint64_t transferredBytes, std::uint64_t totalBytes)> onProgress,
           std::function<void(Result<UploadOutcome>)> onDone) = 0;

    // Fetches the server-side thumbnail of the node identified by handle into
    // the exact local file path destinationPath, same caller-resolves-the-path
    // division of responsibility as download() above. Must not end with a path
    // separator: the SDK would then treat it as a directory and derive the leaf
    // name itself (megaapi_impl.cpp's getNodeAttribute).
    //
    // Unlike download() this is MegaRequestListener-based, so it keeps the
    // single-Result<T>-callback shape of everything above it. Result value is
    // the path actually written (MegaRequest::getFile()).
    //
    // Fails with the SDK's API_ENOENT when the node has no server-side
    // thumbnail; callers are expected to gate on FileEntry::hasThumbnail
    // instead of relying on that, so it is not modeled as a distinct outcome.
    // A handle that no longer resolves is the separate delivery-mode-3 case:
    // onDone on the calling thread, before this call returns.
    virtual void getThumbnail(std::uint64_t handle,
                              const std::string& destinationPath,
                              std::function<void(Result<std::string>)> onDone) = 0;

    // Ancestor chain of the node identified by handle, root-first, always
    // including the node itself as the last element and the root as the
    // first (isRoot == true, handle meaningless -- same sentinel convention
    // as getChildren/search above). Must be called after a successful
    // fetchNodes(). Synchronous under the hood (in-memory parent walk), but
    // kept callback-shaped for interface consistency.
    virtual void getPath(std::uint64_t handle,
                         bool isRoot,
                         std::function<void(Result<std::vector<PathSegment>>)> onDone) = 0;

    // Current identity of the node identified by handle, without walking its
    // ancestors. Fails when the handle resolves to nothing (permanently
    // deleted, or fetchNodes() hasn't run). Succeeding does *not* mean the
    // node is still in the Cloud Drive -- check NodeInfo::inCloud for that,
    // since a deleted node lives on in the Rubbish bin. Must be called after
    // a successful fetchNodes(). Synchronous under the hood (in-memory
    // lookup), but kept callback-shaped for interface consistency.
    virtual void getNodeInfo(std::uint64_t handle,
                             std::function<void(Result<NodeInfo>)> onDone) = 0;

    // First of the mutating calls in this interface -- everything above only
    // reads. All are MegaRequestListener-based, so they keep the
    // single-Result<T>-callback shape; Result<void> because none of them
    // reports anything beyond success/failure. Must be called after a successful
    // fetchNodes(). Unlike the read methods above these do a real API
    // round-trip, so the background-thread caveat at the top of this file is
    // not merely theoretical here -- but only once their handles resolve.
    // Handle resolution itself is delivery mode 3: onDone fires on the calling
    // thread, before the call returns, whenever a handle is already gone.
    virtual void renameNode(std::uint64_t handle,
                            const std::string& newName,
                            std::function<void(Result<void>)> onDone) = 0;

    // "Delete" in MEGA terms: moves the node into the account's Rubbish bin
    // rather than destroying it (MegaApi::remove() would be the permanent
    // one, deliberately not exposed here). A node already in the Rubbish bin
    // is moved to its top level again, which is harmless.
    virtual void moveToRubbish(std::uint64_t handle, std::function<void(Result<void>)> onDone) = 0;

    // Reparents the node identified by handle under newParentHandle
    // (newParentIsRoot makes newParentHandle meaningless, same sentinel
    // convention as getChildren/getPath above). moveToRubbish is really this
    // with the Rubbish bin hardcoded as the destination.
    virtual void moveNode(std::uint64_t handle,
                          std::uint64_t newParentHandle,
                          bool newParentIsRoot,
                          std::function<void(Result<void>)> onDone) = 0;

    // Duplicates the node identified by handle under newParentHandle (same
    // sentinel convention as moveNode). A folder is copied with its whole
    // subtree in this one request -- the SDK walks it server-side, so callers
    // never recurse.
    //
    // newName empty keeps the source's name. Passing one is not cosmetic: when
    // the destination already holds a *file* of that name, the SDK attaches
    // the copy as a new version *over* it instead of creating a sibling, and a
    // byte-identical file is dropped outright while still reporting success.
    // A caller that means "add a second item" must therefore pass a name
    // nothing in the destination uses (FileOperationService::uniqueCopyName).
    // Folders are unaffected -- two same-named folders simply coexist.
    //
    // Unlike createFolder below there is no kEExist to branch on: MEGA permits
    // duplicate sibling names, so the collision is silent either way.
    virtual void copyNode(std::uint64_t handle,
                          std::uint64_t newParentHandle,
                          bool newParentIsRoot,
                          const std::string& newName,
                          std::function<void(Result<void>)> onDone) = 0;

    // Creates an empty folder under parentHandle (parentIsRoot is the same
    // sentinel convention as above).
    //
    // The duplicate-name check is the *server's*: if the parent already
    // contains a folder of that name, the API rejects the request and onDone
    // reports MegaErrorCode::kEExist. That is deliberately the only such
    // check -- an in-memory pre-check against the fetched node tree could go
    // stale between the check and the call, and would be redundant with this
    // one. Note MEGA lets a file and a folder share a name, so an existing
    // *file* called the same thing is not a conflict.
    //
    // The new folder's handle is not reported: the SDK does return it, but no
    // caller needs it (creation is followed by a listing refresh, not by
    // addressing the new node), and Result<void> keeps this on the shared
    // SimpleResultListener path in MegaSdkClient.
    virtual void createFolder(std::uint64_t parentHandle,
                              bool parentIsRoot,
                              const std::string& name,
                              std::function<void(Result<void>)> onDone) = 0;

    // Whether moveNode() with the same arguments would be accepted. Synchronous
    // -- third exception in this interface after currentSessionToken/
    // currentUserHandle, and for the same reason: it's a pure in-memory check
    // against the already-fetched node tree, no API round-trip. It has to be,
    // since a drag hovering over a drop target queries it continuously to paint
    // the "can I drop here" feedback.
    //
    // Failure codes are the interesting part of the result, so they're set
    // precisely (MegaErrorCodes.h): kENoEnt when either end no longer exists,
    // kECircular when a folder would become its own descendant, kEAccess on
    // insufficient permissions, and kEArgs when the node already sits in that
    // folder. Callers branch on errorCode, never on errorMessage.
    //
    // That last case is stricter than the SDK, which accepts a move to the
    // node's current parent as a no-op. It belongs here because an
    // implementation is the only thing that can see a node's actual parent
    // handle; a caller pointing at the root has only the isRoot sentinel.
    virtual Result<void>
    checkMove(std::uint64_t handle, std::uint64_t newParentHandle, bool newParentIsRoot) const = 0;

    // Whether upload() into this folder would be accepted. Synchronous for
    // the same reason as checkMove -- a drag hovering over a drop target
    // queries it continuously -- but unlike checkMove the SDK has no
    // checkUploadErrorExtended equivalent, so the conditions are spelled out
    // by the implementation. Same error-code discipline as checkMove
    // (callers branch on errorCode, never on errorMessage):
    //
    //   kENoEnt  the handle no longer resolves, or resolves to a node that
    //            is no longer in the Cloud Drive (Rubbish bin / Vault)
    //   kEArgs   it resolves to a file, not a folder
    //   kEAccess insufficient permission to add children (read-only share)
    virtual Result<void> checkUpload(std::uint64_t parentHandle, bool parentIsRoot) const = 0;

    // Of names, returns those that already name an existing *file* directly
    // under (parentHandle, parentIsRoot). Neither the order nor the size of
    // the result matches names -- only the hits come back.
    //
    // Same-named *folders* are ignored: uploads are files-only, and MEGA
    // lets a file and a folder share a name, so replacing a folder with a
    // file would be both destructive and unasked-for.
    //
    // Synchronous for the same reason as checkUpload: an in-memory walk of
    // the already-fetched node tree, and drop handling has to decide right
    // there whether to raise a confirmation dialog.
    virtual Result<std::vector<FileEntry>>
    findChildFiles(std::uint64_t parentHandle,
                   bool parentIsRoot,
                   const std::vector<std::string>& names) const = 0;

    // Whether (handle, isRoot) has at least one *folder* child -- the folder
    // tree's own question, since files never appear in the side panel. False
    // for a node that is itself a file.
    //
    // Synchronous, the sixth exception here after checkMove/checkUpload/
    // findChildFiles and for the same reason: an in-memory count over the
    // already-fetched node tree, no API round-trip. It has to be, because
    // QAbstractItemModel::hasChildren() answers the view on the spot and has
    // nowhere to put a callback.
    virtual Result<bool> hasSubfolders(std::uint64_t handle, bool isRoot) const = 0;

    // --- Account-level reads -------------------------------------------------
    //
    // Everything above is auth/session or node tree; these four describe the
    // signed-in *account* itself. Appended rather than grouped next to
    // currentUserHandle() on purpose: the synchronous-exception tally below is
    // positional (checkMove is "the third", hasSubfolders "the sixth"), so
    // inserting higher up would renumber four existing doc comments.

    // Email, the SDK's fallback avatar colour, and the user handle, in one
    // read. MegaApi::getMyEmail + getMyUserHandle + getUserAvatarColor.
    //
    // Synchronous, the seventh exception here, same rationale as
    // currentSessionToken/currentUserHandle: local reads of account state the
    // SDK already holds, no API round-trip. avatarColor in particular is a
    // pure derivation from the user handle, so it is available the instant
    // login completes and never needs the network.
    //
    // Fails if not currently logged in.
    virtual Result<AccountIdentity> currentAccountIdentity() const = 0;

    // Fetches the signed-in account's avatar into the exact local file path
    // destinationPath -- same caller-resolves-the-path division as
    // getThumbnail/download, and the same rule that it must not end with a
    // path separator (the SDK would treat it as a directory and synthesize
    // email + "0.jpg" inside it). Result value is the path actually written.
    //
    // MegaApi::getUserAvatar's active-account overload.
    //
    // Unlike getThumbnail, failure here is *not* exceptional: most accounts
    // have no avatar set, and there is no FileEntry::hasThumbnail-style flag
    // to gate on beforehand, so the outcome is the only signal available.
    // Measured against a real avatar-less account, the code is kENoEnt (-9)
    // with "Not found" -- but megaapi.h does not document that, so callers
    // must treat *any* failure as "no avatar" rather than matching on it.
    // Callers must not surface it as an error -- AccountService converts it to
    // AvatarOutcome::hasAvatar rather than leaving that judgement to each
    // caller.
    virtual void getMyAvatar(const std::string& destinationPath,
                             std::function<void(Result<std::string>)> onDone) = 0;

    // Reads one public attribute of the signed-in account.
    // MegaApi::getUserAttribute's active-account overload; the value arrives
    // in MegaRequest::getText().
    //
    // Parameterised by attribute instead of exposing getMyDisplayName, so that
    // the first-name/last-name join policy lives in AccountService where a
    // mock can test it. Fails when the attribute has never been set, which is
    // ordinary rather than exceptional for names.
    virtual void getMyUserAttribute(UserAttribute attribute,
                                    std::function<void(Result<std::string>)> onDone) = 0;

    // Storage quota and plan level.
    // MegaApi::getSpecificAccountDetails(storage=true, transfer=false,
    // pro=true) -- only two flags because megaapi.h asks callers to request
    // just what they need to minimise server load, and transfer quota is out
    // of scope for this app.
    //
    // Unlike the node-tree getters above, this is a real server round-trip, so
    // the background-thread caveat at the top of this file is not theoretical
    // here. Must not be issued per-frame or from a property getter.
    virtual void getAccountInfo(std::function<void(Result<AccountInfo>)> onDone) = 0;
};
