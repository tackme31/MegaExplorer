#include "MenuActionResolver.h"

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
    // deferred move, hence its decision 1). Recents is the same shape of screen and
    // withholds the same four.
    static const std::vector<MenuActionSpec> actions = {
        {MenuAction::NewFolder,
         {MenuSite::FolderBackground},
         {ViewKind::CloudDrive},
         ActionTarget::FoldersOnly,
         ActionArity::SingleOnly},
        {MenuAction::Download,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents},
         ActionTarget::FilesOnly,
         ActionArity::Any},
        {MenuAction::OpenInNewTab,
         {MenuSite::FileSelection, MenuSite::FolderRow},
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents},
         ActionTarget::FoldersOnly,
         ActionArity::SingleOnly},
        {MenuAction::TogglePin,
         {MenuSite::FileSelection, MenuSite::FolderRow},
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents},
         ActionTarget::FoldersOnly,
         ActionArity::SingleOnly},
        // SingleOnly: with a mixed selection there is no one label to show, and the
        // resolver can't see the flag that would decide it anyway.
        {MenuAction::ToggleFavourite,
         {MenuSite::FileSelection},
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents},
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
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents},
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
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents},
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
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents},
         ActionTarget::Any,
         ActionArity::SingleOnly,
         true},
        {MenuAction::SelectAll,
         {MenuSite::FolderBackground},
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents},
         ActionTarget::FoldersOnly,
         ActionArity::SingleOnly},
        {MenuAction::Refresh,
         {MenuSite::FolderBackground},
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents},
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
    for (const MenuActionSpec& spec : defaultMenuActions())
    {
        // One spec per action, so the first match settles it.
        if (menuActionId(spec.action) == actionId)
            return menuActionApplies(spec, ctx);
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
        case MenuAction::NewFolder:
            return "newFolder";
        case MenuAction::Download:
            return "download";
        case MenuAction::OpenInNewTab:
            return "openInNewTab";
        case MenuAction::TogglePin:
            return "togglePin";
        case MenuAction::ToggleFavourite:
            return "toggleFavourite";
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
    }
    return "";
}
