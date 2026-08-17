#pragma once
#include "IMegaClient.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>

// Mutating counterpart to FolderNavigationService, and deliberately a thin
// validate-then-pass-through layer with no state of its own: fanning one user
// action out over N selected nodes and reconciling the N results belongs to
// src/qml, which is also where results are marshalled back onto the GUI thread.
class FileOperationService
{
public:
    explicit FileOperationService(std::shared_ptr<IMegaClient> client);

    // Fails in-stack, without touching the SDK, when newName fails isValidName()
    // (kEArgs) or a sibling already carries it (kEExist -- unlike copy and move,
    // this app refuses a duplicate rename outright).
    // Callers must skip an unchanged name themselves -- this class can't tell.
    void rename(std::uint64_t handle,
                const std::string& newName,
                std::function<void(Result<void>)> onDone);

    void moveToRubbish(std::uint64_t handle, std::function<void(Result<void>)> onDone);

    // The two irreversible ones, for the Rubbish bin screen. Ungated here on
    // purpose: whether the caller is allowed to offer them is MenuActionResolver's
    // answer, and a second check against a stale view would only disagree with it.
    void removeNode(std::uint64_t handle, std::function<void(Result<void>)> onDone);

    void emptyRubbishBin(std::function<void(Result<void>)> onDone);

    // moveToRubbish's inverse, in two halves because the caller has to word the
    // outcome: this answers *where* the node would go (and whether that is a
    // fallback), and the move itself then goes through move() like any other.
    Result<RestoreTarget> restoreTargetFor(std::uint64_t handle) const;

    // Gated on canMove(), so a caller that skipped the hover-time pre-check still
    // can't issue a move the SDK would only refuse later. An empty newName keeps the
    // source's name; a non-empty one is validated like rename()'s.
    void move(std::uint64_t handle,
              std::uint64_t newParentHandle,
              bool newParentIsRoot,
              const std::string& newName,
              std::function<void(Result<void>)> onDone);

    // An empty newName keeps the source's name; a non-empty one is validated like
    // rename()'s. Gated on canCopy() for the same reason move() is gated on
    // canMove() -- notably a copy of a folder into its own subtree.
    void copy(std::uint64_t handle,
              std::uint64_t newParentHandle,
              bool newParentIsRoot,
              const std::string& newName,
              std::function<void(Result<void>)> onDone);

    // Rejects an invalid name like rename() does, but tags it kEArgs so a caller can
    // tell it from the server's kEExist -- the only duplicate check there is.
    void createFolder(std::uint64_t parentHandle,
                      bool parentIsRoot,
                      const std::string& name,
                      std::function<void(Result<void>)> onDone);

    void
    setFavourite(std::uint64_t handle, bool favourite, std::function<void(Result<void>)> onDone);

    // "Would move() be accepted?", answered without an API round-trip so a drag
    // hovering over a drop target can query it continuously. Failures carry a
    // MegaErrorCodes.h code (kENoEnt / kECircular / kEAccess), not just text.
    Result<void>
    canMove(std::uint64_t handle, std::uint64_t newParentHandle, bool newParentIsRoot) const;

    // "Would copy() be accepted?", synchronous for the same reason. The SDK has no
    // checkCopy, so this reuses checkMove and reinterprets one code: kEArgs
    // ("already in that folder") refuses a move but is a legitimate copy, landing a
    // "... - Copy" sibling. kECircular is the one that matters -- MEGA would happily
    // duplicate a folder into its own subtree.
    //
    // kEAccess is borrowed slightly too eagerly: checkMove also refuses when the
    // *source* can't be removed, which a copy doesn't need. Unreachable while only
    // the Cloud Drive is browsable; revisit if incoming shares become so.
    //
    // Says nothing about whether the destination accepts children -- that is
    // canAddChildren(), and callers ask both.
    Result<void>
    canCopy(std::uint64_t handle, std::uint64_t newParentHandle, bool newParentIsRoot) const;

    // "Would this folder accept a new child?" -- straight through to
    // IMegaClient::checkUpload, whose conditions (gone / not a folder / read-only
    // share) are exactly what a paste must answer before it fans out.
    Result<void> canAddChildren(std::uint64_t parentHandle, bool parentIsRoot) const;

    // How many bytes a copy of this node would add -- the whole sub-tree for a
    // folder. Synchronous, so a conflict dialog can quote it while it is being
    // built (IMegaClient::subtreeSize).
    Result<std::uint64_t> subtreeSizeOf(std::uint64_t handle) const;

    // Deliberately minimal: MEGA permits duplicate names within one folder, so there
    // is no uniqueness check to make here.
    static bool isValidName(const std::string& name);

    // Explorer's answer to a colliding copy: "report.pdf" -> "report - Copy.pdf" ->
    // "report - Copy (2).pdf". Returns name unchanged when taken doesn't hold it.
    // Callers must add the result to their own taken set, since a paste of several
    // same-named nodes has to keep them apart before any reaches the server.
    //
    // Not cosmetic: IMegaClient::copyNode explains why a colliding name silently
    // versions over the existing file instead of landing beside it.
    //
    // The extension splits at the *last* dot and for files only ("archive.tar.gz" ->
    // "archive.tar - Copy.gz", "My.Folder" -> "My.Folder - Copy"); a leading dot is
    // part of the name. The suffix is English because src/core is Qt-free and so out
    // of reach of the .ts files.
    static std::string
    uniqueCopyName(const std::string& name, bool isFolder, const std::set<std::string>& taken);

    // The move path's counterpart: "report.pdf" -> "report (2).pdf" -> "report (3).pdf".
    // A different suffix from uniqueCopyName's because a move is not a duplication and
    // "- Copy" would misdescribe it; the split and the bound are the same
    // (SPEC_NAME_CONFLICT_COPY_MOVE 3-4).
    static std::string
    uniqueMoveName(const std::string& name, bool isFolder, const std::set<std::string>& taken);

private:
    std::shared_ptr<IMegaClient> mClient;
};
