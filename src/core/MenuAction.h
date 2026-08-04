#pragma once

#include <vector>

// Vocabulary for the right-click-menu action resolution logic
// (MenuActionResolver.h). Qt-free like SortOrder.h/PathSegment.h so it stays
// usable from src/core; deliberately just types, no functions.

enum class MenuAction
{
    NewFolder,
    Download,
    OpenInNewTab,
    // One action, not a Pin/Unpin pair: the resolver only sees counts and a
    // site, so it can't know whether the target folder is already pinned. QML
    // resolves that and picks the label, the same C++-decides-applicability /
    // QML-decides-wording split the rest of this table already relies on.
    TogglePin,
    Rename,
    // "Delete" from the user's point of view; named after what it actually
    // does, since MEGA's delete is a move into the Rubbish bin rather than a
    // destructive removal (see IMegaClient::moveToRubbish).
    MoveToRubbish,
};

// Which right-click site a menu is being built for. A site decides *membership*
// only -- the vocabulary and its display order are global (see
// defaultMenuActions()), so the same two actions can never come out in a
// different order depending on where you right-clicked, which is what the old
// hardcoded FolderPinMenu.qml had to be kept in sync by hand to avoid.
enum class MenuSite
{
    // A file view, over its current selection: the one site whose target
    // varies (files/folders x single/multi) and therefore the only reason the
    // ActionTarget x ActionArity axes below exist.
    FileSelection,
    // A file view's empty space -- the folder the view is showing.
    FolderBackground,
    // A folder-tree row or a quick-access pin row -- one folder, addressed by
    // (handle, isRoot).
    FolderRow,
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

struct MenuActionSpec
{
    MenuAction action;
    // Sites this action appears at. Never empty -- an action nothing can
    // reach is a table bug, not a valid state.
    std::vector<MenuSite> sites;
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

struct MenuContext
{
    MenuSite site = MenuSite::FileSelection;
    // For FolderBackground/FolderRow this is the synthesized "one folder"
    // summary (see folderTargetContext()), so target/arity need no
    // site-specific special case: a fixed single-folder target satisfies
    // FoldersOnly/SingleOnly by construction.
    SelectionSummary selection;
};
