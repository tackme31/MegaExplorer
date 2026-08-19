#pragma once
#include "ViewKind.h"

#include <vector>

// Vocabulary for the right-click-menu action resolution in MenuActionResolver.h:
// types only, no functions.

enum class MenuAction
{
    NewFolder,
    Download,
    // Opens the item's counterpart inside the linked local folder with whatever
    // program Windows associates with it. Files only: the shell's answer for a
    // folder is an Explorer window, which is what OpenLocalLocation already gives.
    OpenLocalFile,
    // Shows the item's counterpart inside the local folder linked to the MEGA root,
    // selected in Explorer. Offered whether or not that counterpart exists: the link
    // is a naming convention nothing verifies, and the resolver cannot see the
    // filesystem. Whether a folder is linked at all is QML's to ask (ActionCatalog's
    // `available`), since the setting is app-wide and no MenuContext axis carries it.
    OpenLocalLocation,
    OpenInNewTab,
    // One action, not a Pin/Unpin pair: the resolver sees only counts and a site, so
    // it can't know whether the folder is already pinned. QML picks the label.
    TogglePin,
    // One action like TogglePin above, and for the same reason: the resolver can't
    // see the favourite flag, so QML picks between "add" and "remove".
    ToggleFavourite,
    // Not a toggle pair like the two above, and deliberately: a link is worth
    // re-copying after it exists, so "copy" stays offered either way and "remove"
    // is its own entry. Neither depends on the node's export state, which the
    // listing doesn't carry -- IMegaClient::exportNode and disableExport are both
    // idempotent, so a stale menu still lands where the label promised.
    CopyLink,
    RemoveLink,
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
    // Jumps to the folder the item lives in. Only offered where the rows on screen
    // can come from somewhere other than the folder the view is at -- a search
    // result or the favourites listing -- since anywhere else it would navigate to
    // the folder already open (crossFolderOnly below).
    GoToFolder,
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
    // A bool rather than a third enum axis: unlike target/arity this has no middle
    // value to name. False for every action but GoToFolder.
    bool crossFolderOnly = false;
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
    // Whether the rows on screen may live somewhere other than the location the view
    // is at: a favourites listing, or any listing narrowed by a search. Computed by
    // the caller rather than derived from kind -- a Cloud Drive folder is or isn't
    // cross-folder depending on the search box, which no ViewKind can express.
    bool crossFolderListing = false;
};
