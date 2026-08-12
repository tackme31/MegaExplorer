#pragma once
#include "ViewKind.h"

#include <vector>

// Vocabulary for the right-click-menu action resolution in MenuActionResolver.h:
// types only, no functions.

enum class MenuAction
{
    NewFolder,
    Download,
    OpenInNewTab,
    // One action, not a Pin/Unpin pair: the resolver sees only counts and a site, so
    // it can't know whether the folder is already pinned. QML picks the label.
    TogglePin,
    // One action like TogglePin above, and for the same reason: the resolver can't
    // see the favourite flag, so QML picks between "add" and "remove".
    ToggleFavourite,
    Cut,
    Copy,
    // FolderBackground only, and unconditional there: an empty clipboard is a greying
    // question, not an applicability one, and the resolver can't see the clipboard.
    Paste,
    Rename,
    // "Delete" to the user; named after what it does, since MEGA's delete is a move
    // to the Rubbish bin.
    MoveToRubbish,
    // Rubbish bin only, and MoveToRubbish's inverse. Where each node goes is the
    // SDK's answer, not a choice offered here (IMegaClient::getRestoreTarget).
    Restore,
    // Rubbish bin only. Irreversible, so the scope restriction here is the only
    // thing keeping it off the Cloud Drive's menus -- IMegaClient::removeNode would
    // destroy a live node just as readily.
    DeletePermanently,
    // Rubbish bin only, and the whole bin regardless of where in it the menu was
    // opened: the resolver cannot see whether the view is at the bin's top, and the
    // confirmation names what it will destroy.
    EmptyRubbish,
    // Keyboard shortcuts the view already handles (Ctrl+A, F5); the menu entries call
    // the same thing.
    SelectAll,
    Refresh,
};

// Which right-click site a menu is being built for. A site decides *membership*
// only: the vocabulary and its display order are global, so two actions can never
// come out in a different order depending on where you right-clicked.
enum class MenuSite
{
    // The one site whose target varies (files/folders x single/multi), and therefore
    // the only reason the two axes below exist.
    FileSelection,
    // A file view's empty space: the folder the view is showing.
    FolderBackground,
    // A folder-tree row, a quick-access pin row, or the side panel's Rubbish bin
    // row: one folder. The first two always ask with ViewKind::CloudDrive -- a row
    // names a real folder whatever the file view beside it is showing -- so a
    // Rubbish-scoped entry here reaches only the bin's own row.
    FolderRow,
};

// The two axes an action can be restricted along, orthogonal so that a new
// file-only/folder-only/single-only action needs no case of its own.
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
    // Never empty: an action nothing can reach is a table bug, not a valid state.
    std::vector<MenuSite> sites;
    // Same never-empty contract. A set rather than an "any view" value: a ViewKind
    // added later then starts out offered nowhere -- an empty menu, which is visible --
    // instead of silently inheriting Cloud Drive's answer everywhere.
    std::vector<ViewKind> scopes;
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
    // First so that a positional initializer written before this field existed fails
    // to compile rather than binding site to kind -- MenuSite doesn't convert.
    ViewKind kind = ViewKind::CloudDrive;
    MenuSite site = MenuSite::FileSelection;
    // For FolderBackground/FolderRow this is the synthesized "one folder" summary, so
    // target/arity need no site-specific case: it satisfies FoldersOnly/SingleOnly by
    // construction.
    SelectionSummary selection;
};
