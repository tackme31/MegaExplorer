#pragma once
#include "DownloadOutcome.h"
#include "FileEntry.h"
#include "NodeInfo.h"
#include "PathSegment.h"
#include "Result.h"
#include "SortOrder.h"
#include "UploadOutcome.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Callbacks may be invoked from a background thread, not the caller's thread.
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
    virtual void fetchNodes(std::function<void(Result<void>)> onDone) = 0;

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
    // caveat as the rest of this file applies to both callbacks.
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
    // exactly once, terminally.
    virtual void upload(
        const std::string& localPath,
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

    // First two mutating calls in this interface -- everything above only
    // reads. Both are MegaRequestListener-based, so they keep the
    // single-Result<T>-callback shape; Result<void> because neither reports
    // anything beyond success/failure. Must be called after a successful
    // fetchNodes(). Unlike the read methods above these are genuinely
    // asynchronous (a real API round-trip), so the background-thread caveat
    // at the top of this file is not merely theoretical here.
    virtual void renameNode(std::uint64_t handle,
                            const std::string& newName,
                            std::function<void(Result<void>)> onDone) = 0;

    // "Delete" in MEGA terms: moves the node into the account's Rubbish bin
    // rather than destroying it (MegaApi::remove() would be the permanent
    // one, deliberately not exposed here). A node already in the Rubbish bin
    // is moved to its top level again, which is harmless.
    virtual void moveToRubbish(std::uint64_t handle,
                               std::function<void(Result<void>)> onDone) = 0;

    // Reparents the node identified by handle under newParentHandle
    // (newParentIsRoot makes newParentHandle meaningless, same sentinel
    // convention as getChildren/getPath above). moveToRubbish is really this
    // with the Rubbish bin hardcoded as the destination.
    virtual void moveNode(std::uint64_t handle,
                          std::uint64_t newParentHandle,
                          bool newParentIsRoot,
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
    virtual Result<void> checkMove(std::uint64_t handle,
                                   std::uint64_t newParentHandle,
                                   bool newParentIsRoot) const = 0;

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
};
