#pragma once

// Vocabulary for the selection -> context-menu-actions resolution logic
// (FileActionResolver.h). Qt-free like SortOrder.h/PathSegment.h so it stays
// usable from src/core; deliberately just types, no functions.
enum class FileAction
{
    Download,
    OpenInNewTab,
    // One action, not a Pin/Unpin pair: the resolver only sees a
    // SelectionSummary (counts by kind), so it can't know whether the selected
    // folder is already pinned. QML resolves that and picks the label, the
    // same C++-decides-applicability / QML-decides-wording split the rest of
    // this table already relies on.
    TogglePin,
    Rename,
    // "Delete" from the user's point of view; named after what it actually
    // does, since MEGA's delete is a move into the Rubbish bin rather than a
    // destructive removal (see IMegaClient::moveToRubbish).
    MoveToRubbish,
};

// The two axes a menu action can be restricted along. Orthogonal by design:
// every future action (file-only, folder-only, single-only, multi-only)
// falls out of ActionTarget x ActionArity rather than needing its own case.
enum class ActionTarget
{
    Any,
    FilesOnly,
    FoldersOnly,
};

enum class ActionArity
{
    Any,
    SingleOnly,
    MultiOnly,
};

struct FileActionSpec
{
    FileAction action;
    ActionTarget target;
    ActionArity arity;
};

struct SelectionSummary
{
    int fileCount = 0;
    int folderCount = 0;

    int total() const
    {
        return fileCount + folderCount;
    }
};
