#pragma once
#include "IMegaClient.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>

// Mutating counterpart to FolderNavigationService: renames a node, or moves it
// into the Rubbish bin. Qt-free (MegaExplorerCore) like the other *Service
// classes, so it's testable against MockMegaClient without a GUI.
//
// Deliberately a thin validate-then-pass-through layer with no state of its
// own. Fanning one user action out over N selected nodes and reconciling the N
// results belongs to src/qml (FolderNavigationController::
// moveSelectionToRubbish), same split QuickAccessService.h spells out: services
// pass through, src/qml marshals results back onto the GUI thread.
class FileOperationService
{
public:
    explicit FileOperationService(std::shared_ptr<IMegaClient> client);

    // Calls onDone with a failure immediately -- without touching the SDK --
    // when newName doesn't pass isValidName(). Callers are expected to skip
    // the call entirely when the name is unchanged; this class can't tell,
    // since it doesn't know the node's current name.
    void rename(std::uint64_t handle,
                const std::string& newName,
                std::function<void(Result<void>)> onDone);

    void moveToRubbish(std::uint64_t handle, std::function<void(Result<void>)> onDone);

    // Reparents handle under newParentHandle (newParentIsRoot mirrors the
    // isRoot sentinel convention used throughout). Calls onDone with a failure
    // immediately -- without touching the SDK -- when canMove() rejects the
    // pair, so a caller that skipped the pre-check still can't issue a move the
    // SDK would only refuse later.
    void move(std::uint64_t handle,
              std::uint64_t newParentHandle,
              bool newParentIsRoot,
              std::function<void(Result<void>)> onDone);

    // Copies handle into (newParentHandle, newParentIsRoot). An empty newName
    // keeps the source's name; a non-empty one is validated exactly like
    // rename()'s and rejected with kEArgs without touching the SDK.
    //
    // No destination pre-check here, unlike move() below: circularity is the
    // only per-source condition a move has, and a copy has none, so the
    // caller's single up-front canAddChildren() already covers the whole batch.
    void copy(std::uint64_t handle,
              std::uint64_t newParentHandle,
              bool newParentIsRoot,
              const std::string& newName,
              std::function<void(Result<void>)> onDone);

    // Creates an empty folder under parentHandle. Rejects an invalid name the
    // same way rename() does -- without touching the SDK -- but tags that
    // failure with MegaErrorCode::kEArgs so a caller can tell it apart from a
    // server-side rejection (notably kEExist for a same-named folder, which
    // is the only duplicate check there is; see IMegaClient::createFolder).
    void createFolder(std::uint64_t parentHandle,
                      bool parentIsRoot,
                      const std::string& name,
                      std::function<void(Result<void>)> onDone);

    // Synchronous pre-check, the move counterpart to isValidName() above:
    // "would move() be accepted?", answered without an API round-trip so a drag
    // hovering over a drop target can query it continuously. Failures carry a
    // MegaErrorCodes.h code (kENoEnt / kECircular / kEAccess), not just text.
    Result<void>
    canMove(std::uint64_t handle, std::uint64_t newParentHandle, bool newParentIsRoot) const;

    // "Would this folder accept a new child?", the paste counterpart of
    // canMove() above and synchronous for the same reason. Straight through to
    // IMegaClient::checkUpload, whose conditions (gone / not a folder /
    // read-only share) are exactly the ones a paste has to answer before it
    // fans out.
    Result<void> canAddChildren(std::uint64_t parentHandle, bool parentIsRoot) const;

    // Single definition of the naming rule, static so QML-side pre-validation
    // could share it later. Deliberately minimal: MEGA permits duplicate names
    // within one folder, so there's no uniqueness check to make here.
    static bool isValidName(const std::string& name);

    // Explorer's answer to a copy that would collide: "report.pdf" ->
    // "report - Copy.pdf" -> "report - Copy (2).pdf". Returns name unchanged
    // when taken doesn't hold it, so a copy into a different folder keeps its
    // name. Callers must add the result to their own taken set, since a paste
    // of several same-named nodes has to keep them apart before any of them
    // reaches the server.
    //
    // Not merely cosmetic: IMegaClient::copyNode explains why a colliding name
    // silently versions over the existing file instead of landing beside it.
    //
    // The extension is split at the *last* dot and for files only
    // ("archive.tar.gz" -> "archive.tar - Copy.gz", but "My.Folder" ->
    // "My.Folder - Copy"); a leading dot is part of the name, not an extension
    // (".gitignore" -> ".gitignore - Copy"). The suffix is English rather than
    // translated because src/core is Qt-free and so out of reach of the .ts
    // files -- passing a format string down from QML would be more machinery
    // than the wart is worth.
    static std::string
    uniqueCopyName(const std::string& name, bool isFolder, const std::set<std::string>& taken);

private:
    std::shared_ptr<IMegaClient> mClient;
};
