#include "MenuActionResolver.h"

#include "OpenWithEntry.h"

#include <algorithm>

namespace
{

bool siteMatches(const std::vector<MenuSite>& sites, MenuSite site)
{
    return std::find(sites.begin(), sites.end(), site) != sites.end();
}

bool scopeMatches(const std::vector<ViewKind>& scopes, ViewKind kind)
{
    return std::find(scopes.begin(), scopes.end(), kind) != scopes.end();
}

bool targetMatches(ActionTarget target, const SelectionSummary& selection)
{
    switch (target)
    {
        case ActionTarget::Any:
            return true;
        case ActionTarget::FilesOnly:
            return selection.folderCount == 0;
        case ActionTarget::FoldersOnly:
            return selection.fileCount == 0;
    }
    return false;
}

bool arityMatches(ActionArity arity, const SelectionSummary& selection)
{
    switch (arity)
    {
        case ActionArity::Any:
            return true;
        case ActionArity::SingleOnly:
            return selection.total() == 1;
        case ActionArity::MultiOnly:
            return selection.total() > 1;
    }
    return false;
}

} // namespace

bool menuActionApplies(const MenuActionSpec& spec, const MenuContext& ctx)
{
    if (!siteMatches(spec.sites, ctx.site))
        return false;

    if (!scopeMatches(spec.scopes, ctx.kind))
        return false;

    if (ctx.selection.total() == 0)
        return false;

    if (spec.crossFolderOnly && !ctx.crossFolderListing)
        return false;

    return targetMatches(spec.target, ctx.selection) && arityMatches(spec.arity, ctx.selection);
}

const std::vector<MenuActionSpec>& defaultMenuActions()
{
    // The four actions a favourites listing withholds -- NewFolder, Cut, Paste,
    // MoveToRubbish -- are exactly the ones needing a destination folder, which a
    // flat cross-drive listing has none of (FAVOURITES_VIEW_SPEC.md 4.1; Cut is a
    // deferred move, hence its decision 1). Recents and the public-link listing are the
    // same shape of screen and withhold the same four.
    static const std::vector<MenuActionSpec> actions = {
        // First, as in Explorer. Rubbish included: double-click opens a binned file too.
        {MenuAction::Open,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive,
          ViewKind::Favourites,
          ViewKind::Recents,
          ViewKind::SharedLinks,
          ViewKind::Rubbish},
         ActionTarget::FilesOnly,
         ActionArity::SingleOnly},
        // Offered on exactly Open's terms, whatever the name: the point is a file whose
        // extension is wrong or missing.
        {MenuAction::OpenAsImage,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive,
          ViewKind::Favourites,
          ViewKind::Recents,
          ViewKind::SharedLinks,
          ViewKind::Rubbish},
         ActionTarget::FilesOnly,
         ActionArity::SingleOnly},
        {MenuAction::OpenAsVideo,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive,
          ViewKind::Favourites,
          ViewKind::Recents,
          ViewKind::SharedLinks,
          ViewKind::Rubbish},
         ActionTarget::FilesOnly,
         ActionArity::SingleOnly},
        {MenuAction::OpenAsAudio,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive,
          ViewKind::Favourites,
          ViewKind::Recents,
          ViewKind::SharedLinks,
          ViewKind::Rubbish},
         ActionTarget::FilesOnly,
         ActionArity::SingleOnly},
        {MenuAction::OpenAsPdf,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive,
          ViewKind::Favourites,
          ViewKind::Recents,
          ViewKind::SharedLinks,
          ViewKind::Rubbish},
         ActionTarget::FilesOnly,
         ActionArity::SingleOnly},
        {MenuAction::OpenAsArchive,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive,
          ViewKind::Favourites,
          ViewKind::Recents,
          ViewKind::SharedLinks,
          ViewKind::Rubbish},
         ActionTarget::FilesOnly,
         ActionArity::SingleOnly},
        {MenuAction::OpenWithBrowser,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive,
          ViewKind::Favourites,
          ViewKind::Recents,
          ViewKind::SharedLinks,
          ViewKind::Rubbish},
         ActionTarget::FilesOnly,
         ActionArity::SingleOnly},
        {MenuAction::OpenWithCustom,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive,
          ViewKind::Favourites,
          ViewKind::Recents,
          ViewKind::SharedLinks,
          ViewKind::Rubbish},
         ActionTarget::FilesOnly,
         ActionArity::SingleOnly},
        {MenuAction::NewFolder,
         {MenuSite::FolderBackground},
         {ViewKind::CloudDrive},
         ActionTarget::FoldersOnly,
         ActionArity::SingleOnly},
        {MenuAction::Download,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents, ViewKind::SharedLinks},
         ActionTarget::FilesOnly,
         ActionArity::Any},
        // SingleOnly, unlike Download above: each handle resolves through its own
        // asynchronous getPath, so a multi-selection with no local counterpart would
        // answer with one toast per item.
        {MenuAction::OpenLocalFile,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents, ViewKind::SharedLinks},
         ActionTarget::FilesOnly,
         ActionArity::SingleOnly},
        // SingleOnly: Explorer selects one item per window, so a multi-selection
        // could only reveal the last of them. Rubbish is left out -- a binned node
        // has no counterpart under the linked folder.
        {MenuAction::OpenLocalLocation,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents, ViewKind::SharedLinks},
         ActionTarget::Any,
         ActionArity::SingleOnly},
        {MenuAction::OpenInNewTab,
         {MenuSite::FileSelection, MenuSite::FolderRow},
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents, ViewKind::SharedLinks},
         ActionTarget::FoldersOnly,
         ActionArity::SingleOnly},
        {MenuAction::TogglePin,
         {MenuSite::FileSelection, MenuSite::FolderRow},
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents, ViewKind::SharedLinks},
         ActionTarget::FoldersOnly,
         ActionArity::SingleOnly},
        // SingleOnly: with a mixed selection there is no one label to show, and the
        // resolver can't see the flag that would decide it anyway.
        {MenuAction::ToggleFavourite,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents, ViewKind::SharedLinks},
         ActionTarget::Any,
         ActionArity::SingleOnly},
        // The three link actions end up in one "Share" submenu (ActionCatalog's
        // `groups`), which keeps this order.
        {MenuAction::LinkSettings,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents, ViewKind::SharedLinks},
         ActionTarget::Any,
         ActionArity::SingleOnly},
        // SingleOnly for a reason the other two SingleOnly actions don't have: the
        // clipboard holds one string, so a multi-selection could only leave the last
        // link there. Rubbish is left out -- a binned node's link is not something
        // to hand anyone.
        {MenuAction::CopyLink,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents, ViewKind::SharedLinks},
         ActionTarget::Any,
         ActionArity::SingleOnly},
        // Last of the three so the destructive one sits at the bottom of the group.
        {MenuAction::RemoveLink,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents, ViewKind::SharedLinks},
         ActionTarget::Any,
         ActionArity::SingleOnly},
        // Cut before Copy, Windows' own order.
        {MenuAction::Cut,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive},
         ActionTarget::Any,
         ActionArity::Any},
        {MenuAction::Copy,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents, ViewKind::SharedLinks},
         ActionTarget::Any,
         ActionArity::Any},
        // FoldersOnly/SingleOnly like NewFolder, satisfied the same way:
        // folderTargetContext() synthesizes exactly that selection.
        {MenuAction::Paste,
         {MenuSite::FolderBackground},
         {ViewKind::CloudDrive},
         ActionTarget::FoldersOnly,
         ActionArity::SingleOnly},
        // SingleOnly is the whole implementation of "no rename while multiple items
        // are selected".
        {MenuAction::Rename,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents, ViewKind::SharedLinks},
         ActionTarget::Any,
         ActionArity::SingleOnly},
        {MenuAction::MoveToRubbish,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive},
         ActionTarget::Any,
         ActionArity::Any},
        {MenuAction::Restore,
         {MenuSite::FileSelection},
         {ViewKind::Rubbish},
         ActionTarget::Any,
         ActionArity::Any},
        {MenuAction::DeletePermanently,
         {MenuSite::FileSelection},
         {ViewKind::Rubbish},
         ActionTarget::Any,
         ActionArity::Any},
        // FoldersOnly/SingleOnly like NewFolder, and satisfied the same way:
        // folderTargetContext() synthesizes exactly that selection for both sites.
        {MenuAction::EmptyRubbish,
         {MenuSite::FolderBackground, MenuSite::FolderRow},
         {ViewKind::Rubbish},
         ActionTarget::FoldersOnly,
         ActionArity::SingleOnly},
        // Rubbish is left out on purpose: the bin is flat and its rows' original
        // parents are gone, which is what Restore exists to answer.
        {MenuAction::GoToFolder,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents, ViewKind::SharedLinks},
         ActionTarget::Any,
         ActionArity::SingleOnly,
         true},
        {MenuAction::SelectAll,
         {MenuSite::FolderBackground},
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents, ViewKind::SharedLinks},
         ActionTarget::FoldersOnly,
         ActionArity::SingleOnly},
        // FolderRow too, where it re-reads that row's subfolders instead of the
        // listing. Reaches a Quick access pin as well, which has no subtree to
        // re-read; hiding it there is QML's job (ActionCatalog's `available`),
        // since "is this row a tree row" is not something MenuContext carries.
        {MenuAction::Refresh,
         {MenuSite::FolderBackground, MenuSite::FolderRow},
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents, ViewKind::SharedLinks},
         ActionTarget::FoldersOnly,
         ActionArity::SingleOnly},
        // SingleOnly: the dialog describes one node, and Rubbish is included --
        // unlike the other cross-view actions -- because reading a binned node's
        // size and location is exactly when it is wanted.
        {MenuAction::Properties,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive,
          ViewKind::Favourites,
          ViewKind::Recents,
          ViewKind::SharedLinks,
          ViewKind::Rubbish},
         ActionTarget::Any,
         ActionArity::SingleOnly},
        // The same action from a view's empty space, describing the folder on
        // screen. A second row rather than another site on the one above, because
        // the sites disagree about scope: only the Cloud Drive's background always
        // names a node the lookup can resolve. Favourites and Recents synthesize a
        // handle-less location, and the Rubbish bin's own top is handle 0 with
        // isRoot set -- which MegaSdkClient::resolveNode answers with the *Cloud
        // Drive* root, so offering it there would quietly describe the wrong node.
        {MenuAction::Properties,
         {MenuSite::FolderBackground},
         {ViewKind::CloudDrive},
         ActionTarget::FoldersOnly,
         ActionArity::SingleOnly},
    };
    return actions;
}

std::vector<MenuAction> resolveMenuActions(const MenuContext& ctx,
                                           const std::vector<MenuActionSpec>& specs)
{
    std::vector<MenuAction> result;
    for (const MenuActionSpec& spec : specs)
    {
        if (menuActionApplies(spec, ctx))
            result.push_back(spec.action);
    }
    return result;
}

bool menuActionAllowed(std::string_view actionId, const MenuContext& ctx)
{
    // A user-registered program's ID carries its index; applicability is the same
    // for all of them, so the suffix is dropped before the table is consulted.
    // Without this the keyboard path would answer no for every one of them.
    if (openWithCustomIndex(actionId) >= 0)
        actionId = menuActionId(MenuAction::OpenWithCustom);

    for (const MenuActionSpec& spec : defaultMenuActions())
    {
        // An action may have more than one row (Properties has one per site), so
        // every row for the ID is tried before answering no -- stopping at the first
        // would report a site's own row as unreachable.
        if (menuActionId(spec.action) == actionId && menuActionApplies(spec, ctx))
            return true;
    }
    return false;
}

MenuContext folderTargetContext(MenuSite site, ViewKind kind)
{
    MenuContext ctx;
    ctx.kind = kind;
    ctx.site = site;
    ctx.selection.fileCount = 0;
    ctx.selection.folderCount = 1;
    return ctx;
}

const char* menuActionId(MenuAction action)
{
    switch (action)
    {
        case MenuAction::Open:
            return "open";
        case MenuAction::OpenAsImage:
            return "openAsImage";
        case MenuAction::OpenAsVideo:
            return "openAsVideo";
        case MenuAction::OpenAsAudio:
            return "openAsAudio";
        case MenuAction::OpenAsPdf:
            return "openAsPdf";
        case MenuAction::OpenAsArchive:
            return "openAsArchive";
        case MenuAction::OpenWithBrowser:
            return "openWithBrowser";
        case MenuAction::OpenWithCustom:
            return "openWithCustom";
        case MenuAction::NewFolder:
            return "newFolder";
        case MenuAction::Download:
            return "download";
        case MenuAction::OpenLocalFile:
            return "openLocalFile";
        case MenuAction::OpenLocalLocation:
            return "openLocalLocation";
        case MenuAction::OpenInNewTab:
            return "openInNewTab";
        case MenuAction::TogglePin:
            return "togglePin";
        case MenuAction::ToggleFavourite:
            return "toggleFavourite";
        case MenuAction::CopyLink:
            return "copyLink";
        case MenuAction::LinkSettings:
            return "linkSettings";
        case MenuAction::RemoveLink:
            return "removeLink";
        case MenuAction::Cut:
            return "cut";
        case MenuAction::Copy:
            return "copy";
        case MenuAction::Paste:
            return "paste";
        case MenuAction::Rename:
            return "rename";
        case MenuAction::MoveToRubbish:
            return "moveToRubbish";
        case MenuAction::Restore:
            return "restore";
        case MenuAction::DeletePermanently:
            return "deletePermanently";
        case MenuAction::EmptyRubbish:
            return "emptyRubbish";
        case MenuAction::GoToFolder:
            return "goToFolder";
        case MenuAction::SelectAll:
            return "selectAll";
        case MenuAction::Refresh:
            return "refresh";
        case MenuAction::Properties:
            return "properties";
    }
    return "";
}
