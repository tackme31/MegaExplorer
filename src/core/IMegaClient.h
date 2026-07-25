#pragma once
#include "DownloadOutcome.h"
#include "FileEntry.h"
#include "Result.h"

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

    // Must be called after a successful login(), before getRootChildren().
    virtual void fetchNodes(std::function<void(Result<void>)> onDone) = 0;

    // Must be called after a successful fetchNodes(). Synchronous under the
    // hood, but kept callback-shaped for interface consistency.
    virtual void getRootChildren(std::function<void(Result<std::vector<FileEntry>>)> onDone) = 0;

    // Must be called after a successful fetchNodes(). Synchronous under the
    // hood, but kept callback-shaped for interface consistency.
    virtual void getChildren(std::uint64_t handle,
                             std::function<void(Result<std::vector<FileEntry>>)> onDone) = 0;

    // Recursive name search rooted at ancestorHandle (ignored when isRoot is
    // true, same isRoot-sentinel convention as FolderNavigationService's
    // Location). Must be called after a successful fetchNodes(). Synchronous
    // under the hood (MegaApi::search()), but kept callback-shaped for
    // interface consistency.
    virtual void search(std::uint64_t ancestorHandle,
                        bool isRoot,
                        const std::string& query,
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
};
