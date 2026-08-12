#pragma once
#include "AccountInfo.h"
#include "DownloadOutcome.h"
#include "FileEntry.h"
#include "NodeInfo.h"
#include "PathSegment.h"
#include "RestoreTarget.h"
#include "Result.h"
#include "SortOrder.h"
#include "UploadOutcome.h"
#include "UserAttribute.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Two conventions run through this whole interface.
//
// 1. An `isRoot`/`parentIsRoot` flag set to true makes the handle beside it
//    meaningless -- the sentinel for "the Cloud Drive root".
// 2. Callback delivery is not uniform, and a caller that assumes "asynchronous"
//    is wrong in two of the three cases:
//      - Methods returning Result<T> with no callback answer in-stack always:
//        local reads of session/account state, or in-memory queries against the
//        node tree fetchNodes built. Their callers (drag-hover feedback,
//        QAbstractItemModel::hasChildren) have nowhere to put a callback.
//      - Of the methods taking onDone, getRootChildren/getChildren/search/
//        listFavourites/getPath/getNodeInfo run it synchronously on the calling
//        thread, always -- they too are in-memory reads.
//        FolderNavigationService's lock-free design rests on this.
//      - The rest run onDone on an SDK-internal thread, *except* that any
//        method resolving a handle fails in-stack when that handle is already
//        gone. That case is easy to miss: the happy path looks purely async.
//
// So onDone may run inside your own call, before it returns: don't hold a lock
// across the call, and guard against re-entering the code that issued it
// (DownloadService/UploadService/ThumbnailService each carry a trampoline).
class IMegaClient
{
public:
    virtual ~IMegaClient() = default;

    virtual void login(const std::string& email,
                       const std::string& password,
                       std::function<void(Result<void>)> onDone) = 0;

    // MegaApi::fastLogin equivalent: re-authenticates from a token previously
    // obtained via currentSessionToken(), skipping password verification.
    virtual void loginWithSession(const std::string& sessionToken,
                                  std::function<void(Result<void>)> onDone) = 0;

    // Called only after login() failed with MegaErrorCode::kEMfaRequired,
    // resubmitting the same email/password alongside the 6-digit pin.
    virtual void multiFactorAuthLogin(const std::string& email,
                                      const std::string& password,
                                      const std::string& pin,
                                      std::function<void(Result<void>)> onDone) = 0;

    virtual void logout(std::function<void(Result<void>)> onDone) = 0;

    // MegaApi::dumpSession equivalent. Fails if not currently logged in.
    virtual Result<std::string> currentSessionToken() const = 0;

    // MegaApi::getMyUserHandleBinary equivalent. Fails if not logged in. Scopes
    // per-account state (quick-access pins) so identity stays out of ISessionStore.
    virtual Result<std::uint64_t> currentUserHandle() const = 0;

    // Must be called after a successful login(), before getRootChildren().
    //
    // onProgress is narrower than it looks: it is HTTP progress of the `f` API
    // response body, not "nodes processed" -- the decrypt/tree-build that follows
    // has no progress signal at all and was 57% of wall time on a 640k-node
    // account. It also may fire zero times (a valid local state-cache DB skips
    // the network entirely), and its last event need not reach 100%.
    // Measurements: docs/investigations/FETCHNODES_PROGRESS_INVESTIGATION.md.
    virtual void fetchNodes(
        std::function<void(std::uint64_t transferredBytes, std::uint64_t totalBytes)> onProgress,
        std::function<void(Result<void>)> onDone) = 0;

    // MegaApi::catchup: applies action packets not yet received, so the getters
    // below read a current tree. The only genuine round-trip among the reads --
    // the getters alone re-read whatever the SDK happens to have been told.
    virtual void syncPendingChanges(std::function<void(Result<void>)> onDone) = 0;

    // Must be called after a successful fetchNodes(). order is forwarded to
    // MegaApi::getChildren's own order argument (server-side sort, SortOrder.h).
    virtual void getRootChildren(SortOrder order,
                                 std::function<void(Result<std::vector<FileEntry>>)> onDone) = 0;

    // Same contract as getRootChildren(), for a specific folder.
    virtual void getChildren(std::uint64_t handle,
                             SortOrder order,
                             std::function<void(Result<std::vector<FileEntry>>)> onDone) = 0;

    // Recursive name search rooted at ancestorHandle. Must be called after a
    // successful fetchNodes(); order is forwarded to MegaApi::search's own order.
    virtual void search(std::uint64_t ancestorHandle,
                        bool isRoot,
                        const std::string& query,
                        SortOrder order,
                        std::function<void(Result<std::vector<FileEntry>>)> onDone) = 0;

    // Every favourite under the Cloud Drive root, recursively; same contract as
    // search(). Rooting it there is what keeps the Rubbish bin, the Vault and
    // incoming shares out. An empty nameFilter means no name filtering at all --
    // MegaSearchFilter treats an unset name as "match everything", so the
    // favourite flag stays the only criterion.
    virtual void listFavourites(SortOrder order,
                                const std::string& nameFilter,
                                std::function<void(Result<std::vector<FileEntry>>)> onDone) = 0;

    // The Rubbish bin's own top level, same contract as getRootChildren(). Only the
    // top needs its own call: everything below it is an ordinary node, so going
    // deeper is getChildren() with the handle, exactly as in the Cloud Drive.
    virtual void
    getRubbishChildren(SortOrder order,
                       std::function<void(Result<std::vector<FileEntry>>)> onDone) = 0;

    // Where restoring a binned node would put it: the folder it was in when it was
    // binned (MegaNode::getRestoreHandle). Synchronous, like the other in-memory
    // queries. isRoot follows the usual convention, and is the answer whenever the
    // original parent is gone or was never recorded -- MEGA's own clients fall back
    // to the Cloud Drive root rather than refusing, and refusing here would strand
    // the node with no other way out of the bin. Fails only when the node itself is
    // gone.
    virtual Result<RestoreTarget> getRestoreTarget(std::uint64_t handle) const = 0;

    // Downloads to the exact local path destinationPath -- the caller resolves it,
    // since IMegaClient has no filesystem access of its own.
    //
    // MegaApi::startDownload is MegaTransferListener-based, hence the second
    // callback: onProgress may fire zero or more times, onDone exactly once.
    // DownloadOutcome::localPath is the path the SDK *actually* wrote
    // (MegaTransfer::getPath()), which differs from destinationPath when a name
    // collision made the SDK rename the file; alreadyPresent instead means the
    // SDK fingerprinted an identical file there and skipped the download.
    virtual void download(
        std::uint64_t handle,
        const std::string& destinationPath,
        std::function<void(std::uint64_t transferredBytes, std::uint64_t totalBytes)> onProgress,
        std::function<void(Result<DownloadOutcome>)> onDone) = 0;

    // Uploads the local *file* at localPath (caller-resolved, absolute, native
    // separators) into the folder parentHandle.
    //
    // Files only: MegaApi::startUpload also accepts a directory and would upload
    // it recursively, which this app never exposes -- callers must filter
    // directories out beforehand.
    //
    // Two-callback shape for the same reason as download().
    virtual void
    upload(const std::string& localPath,
           std::uint64_t parentHandle,
           bool parentIsRoot,
           std::function<void(std::uint64_t transferredBytes, std::uint64_t totalBytes)> onProgress,
           std::function<void(Result<UploadOutcome>)> onDone) = 0;

    // Fetches the server-side thumbnail into the exact local path destinationPath,
    // which must not end with a path separator: the SDK would then treat it as a
    // directory and derive the leaf name itself (megaapi_impl.cpp's
    // getNodeAttribute). Result value is the path actually written.
    //
    // Fails with API_ENOENT when the node has no server-side thumbnail; callers
    // gate on FileEntry::hasThumbnail rather than branching on that.
    virtual void getThumbnail(std::uint64_t handle,
                              const std::string& destinationPath,
                              std::function<void(Result<std::string>)> onDone) = 0;

    // Same call shape and same separator rule as getThumbnail, but the attribute
    // fetched is the preview: a JPEG scaled to fit inside 1000x1000, not the 200px
    // square crop.
    //
    // Unlike getThumbnail there is no FileEntry flag to gate on, and a preview only
    // exists when the uploading client generated one. Callers must therefore read
    // *any* failure as "no preview" and must not surface it as an error -- the same
    // rule getMyAvatar states.
    virtual void getPreview(std::uint64_t handle,
                            const std::string& destinationPath,
                            std::function<void(Result<std::string>)> onDone) = 0;

    // Reads a whole file into memory instead of writing it anywhere
    // (MegaApi::startStreaming). The only caller is the preview pane's text view,
    // which must leave nothing behind on disk.
    //
    // maxBytes is a hard stop, not a range request: exceeding it aborts the transfer
    // and fails. Callers gate on FileEntry::sizeBytes first, so this is the check
    // against a node bigger than the listing claimed.
    //
    // There is deliberately no way to cancel. The payload is capped in the tens of
    // kilobytes, so there is no bandwidth worth reclaiming, and a cancel channel
    // would need shared state the SDK thread polls -- the cross-thread lifetime
    // problem this design exists to avoid.
    virtual void readFileContent(std::uint64_t handle,
                                 std::uint64_t maxBytes,
                                 std::function<void(Result<std::vector<char>>)> onDone) = 0;

    // Ancestor chain root-first, always including the root as the first element
    // and the node itself as the last. Must follow a successful fetchNodes().
    virtual void getPath(std::uint64_t handle,
                         bool isRoot,
                         std::function<void(Result<std::vector<PathSegment>>)> onDone) = 0;

    // Current identity of the node, without walking its ancestors. Succeeding does
    // *not* mean the node is still in the Cloud Drive -- a deleted node lives on
    // in the Rubbish bin, so check NodeInfo::inCloud for that.
    virtual void getNodeInfo(std::uint64_t handle,
                             std::function<void(Result<NodeInfo>)> onDone) = 0;

    // First of the mutating calls -- everything above only reads. All are real API
    // round-trips and must follow a successful fetchNodes().
    virtual void renameNode(std::uint64_t handle,
                            const std::string& newName,
                            std::function<void(Result<void>)> onDone) = 0;

    // "Delete" in MEGA terms: moves the node to the Rubbish bin rather than
    // destroying it. The permanent form is removeNode() below.
    virtual void moveToRubbish(std::uint64_t handle, std::function<void(Result<void>)> onDone) = 0;

    // Destroys the node outright (MegaApi::remove). Irreversible -- MEGA keeps no
    // second bin behind the bin -- so every caller must confirm first. The SDK will
    // happily remove a node that is not in the bin, so the restriction to the bin
    // lives in MenuActionResolver rather than here.
    virtual void removeNode(std::uint64_t handle, std::function<void(Result<void>)> onDone) = 0;

    // Empties the Rubbish bin in one request (MegaApi::cleanRubbishBin) rather than
    // a removeNode() per child: it is server-side, so it neither enumerates the bin
    // nor leaves it half-emptied if the app quits mid-way. Irreversible.
    virtual void cleanRubbishBin(std::function<void(Result<void>)> onDone) = 0;

    virtual void moveNode(std::uint64_t handle,
                          std::uint64_t newParentHandle,
                          bool newParentIsRoot,
                          std::function<void(Result<void>)> onDone) = 0;

    // Duplicates the node under newParentHandle. A folder is copied with its whole
    // subtree in this one request -- the SDK walks it server-side.
    //
    // newName empty keeps the source's name, and passing one is not cosmetic: if
    // the destination already holds a *file* of that name, the SDK attaches the
    // copy as a new version *over* it instead of creating a sibling, and drops a
    // byte-identical file outright while still reporting success. "Add a second
    // item" therefore requires an unused name (FileOperationService::uniqueCopyName).
    // Folders are unaffected -- two same-named folders coexist, with no kEExist.
    virtual void copyNode(std::uint64_t handle,
                          std::uint64_t newParentHandle,
                          bool newParentIsRoot,
                          const std::string& newName,
                          std::function<void(Result<void>)> onDone) = 0;

    // Creates an empty folder under parentHandle. The duplicate-name check is the
    // *server's* -- onDone reports MegaErrorCode::kEExist -- and deliberately the
    // only one: an in-memory pre-check could go stale between check and call. Note
    // MEGA lets a file and a folder share a name, so an existing *file* of that
    // name is not a conflict.
    virtual void createFolder(std::uint64_t parentHandle,
                              bool parentIsRoot,
                              const std::string& name,
                              std::function<void(Result<void>)> onDone) = 0;

    // Sets or clears the node's "favourite" attribute. Idempotent -- the SDK reports
    // success for a value the node already has, so a caller whose cached flag has
    // drifted still ends up with the state it asked for.
    virtual void setNodeFavourite(std::uint64_t handle,
                                  bool favourite,
                                  std::function<void(Result<void>)> onDone) = 0;

    // Whether moveNode() with the same arguments would be accepted, as a pure
    // in-memory check -- a drag hovering over a drop target queries it per mouse
    // move. Callers branch on errorCode, never errorMessage (MegaErrorCodes.h):
    // kENoEnt either end is gone, kECircular a folder would become its own
    // descendant, kEAccess insufficient permission, kEArgs the node already sits
    // in that folder. That last case is stricter than the SDK, which treats it as
    // a no-op; it lives here because only an implementation can see a node's
    // actual parent handle.
    virtual Result<void>
    checkMove(std::uint64_t handle, std::uint64_t newParentHandle, bool newParentIsRoot) const = 0;

    // Whether upload() into this folder would be accepted. The SDK has no
    // checkUploadErrorExtended equivalent, so the implementation spells the
    // conditions out itself; same error-code discipline as checkMove:
    //
    //   kENoEnt  the handle no longer resolves, or resolves to a node that
    //            is no longer in the Cloud Drive (Rubbish bin / Vault)
    //   kEArgs   it resolves to a file, not a folder
    //   kEAccess insufficient permission to add children (read-only share)
    virtual Result<void> checkUpload(std::uint64_t parentHandle, bool parentIsRoot) const = 0;

    // Of names, returns those that already name an existing *file* directly under
    // the given parent -- only the hits come back, in no particular order.
    //
    // Same-named *folders* are ignored: uploads are files-only and MEGA lets a
    // file and a folder share a name, so replacing a folder would be destructive
    // and unasked-for.
    virtual Result<std::vector<FileEntry>>
    findChildFiles(std::uint64_t parentHandle,
                   bool parentIsRoot,
                   const std::vector<std::string>& names) const = 0;

    // Whether the node has at least one *folder* child -- the folder tree's own
    // question, since files never appear in the side panel. False for a file.
    virtual Result<bool> hasSubfolders(std::uint64_t handle, bool isRoot) const = 0;

    // --- Account-level reads -------------------------------------------------

    // MegaApi::getMyEmail + getMyUserHandle + getUserAvatarColor in one read.
    // Fails if not currently logged in.
    //
    // Not an atomic snapshot: each field is a separate SDK read under its own
    // lock, so a logout landing mid-call can pair one account's email with an
    // empty handle. Acceptable only because the sole consumer paints the account
    // panel that the same logout tears down; anything that *decides* on the pair
    // needs a snapshotting variant.
    virtual Result<AccountIdentity> currentAccountIdentity() const = 0;

    // Fetches the signed-in account's avatar into the exact local path
    // destinationPath, which must not end with a path separator (the SDK would
    // treat it as a directory and synthesize email + "0.jpg" inside it). Result
    // value is the path actually written.
    //
    // Unlike getThumbnail, failure is *not* exceptional: most accounts have no
    // avatar and there is no flag to gate on beforehand. The observed code is
    // kENoEnt, but megaapi.h does not document that, so callers must read *any*
    // failure as "no avatar" -- AccountService converts it to
    // AvatarOutcome::hasAvatar rather than leaving that to each caller.
    virtual void getMyAvatar(const std::string& destinationPath,
                             std::function<void(Result<std::string>)> onDone) = 0;

    // Reads one public attribute of the signed-in account; the value arrives in
    // MegaRequest::getText(). Parameterised by attribute rather than exposing
    // getMyDisplayName, so the first-name/last-name join policy stays in
    // AccountService where a mock can test it. Fails when the attribute has never
    // been set, which is ordinary for names.
    virtual void getMyUserAttribute(UserAttribute attribute,
                                    std::function<void(Result<std::string>)> onDone) = 0;

    // Storage quota and plan level. MegaApi::getSpecificAccountDetails with only
    // storage+pro: megaapi.h asks callers to request just what they need, and
    // transfer quota is out of scope. A real server round-trip -- never per-frame
    // or from a property getter.
    virtual void getAccountInfo(std::function<void(Result<AccountInfo>)> onDone) = 0;
};
