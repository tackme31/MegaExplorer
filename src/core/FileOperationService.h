#pragma once
#include "IMegaClient.h"

#include <cstdint>
#include <functional>
#include <memory>
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

    // Synchronous pre-check, the move counterpart to isValidName() above:
    // "would move() be accepted?", answered without an API round-trip so a drag
    // hovering over a drop target can query it continuously. Failures carry a
    // MegaErrorCodes.h code (kENoEnt / kECircular / kEAccess), not just text.
    Result<void> canMove(std::uint64_t handle,
                         std::uint64_t newParentHandle,
                         bool newParentIsRoot) const;

    // Single definition of the naming rule, static so QML-side pre-validation
    // could share it later. Deliberately minimal: MEGA permits duplicate names
    // within one folder, so there's no uniqueness check to make here.
    static bool isValidName(const std::string& name);

private:
    std::shared_ptr<IMegaClient> mClient;
};
