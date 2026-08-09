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
    // A folder-tree row or a quick-access pin row: one folder.
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
