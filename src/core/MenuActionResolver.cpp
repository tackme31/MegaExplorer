#include "MenuActionResolver.h"

#include <algorithm>

namespace
{

bool siteMatches(const std::vector<MenuSite>& sites, MenuSite site)
{
    return std::find(sites.begin(), sites.end(), site) != sites.end();
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

    if (ctx.selection.total() == 0)
        return false;

    return targetMatches(spec.target, ctx.selection) && arityMatches(spec.arity, ctx.selection);
}

const std::vector<MenuActionSpec>& defaultMenuActions()
{
    static const std::vector<MenuActionSpec> actions = {
        {MenuAction::NewFolder,
         {MenuSite::FolderBackground},
         ActionTarget::FoldersOnly,
         ActionArity::SingleOnly},
        {MenuAction::Download,
         {MenuSite::FileSelection},
         ActionTarget::FilesOnly,
         ActionArity::Any},
        {MenuAction::OpenInNewTab,
         {MenuSite::FileSelection, MenuSite::FolderRow},
         ActionTarget::FoldersOnly,
         ActionArity::SingleOnly},
        {MenuAction::TogglePin,
         {MenuSite::FileSelection, MenuSite::FolderRow},
         ActionTarget::FoldersOnly,
         ActionArity::SingleOnly},
        // SingleOnly is the whole implementation of "no rename while multiple
        // items are selected" -- the resolver itself needed no change.
        {MenuAction::Rename, {MenuSite::FileSelection}, ActionTarget::Any, ActionArity::SingleOnly},
        {MenuAction::MoveToRubbish, {MenuSite::FileSelection}, ActionTarget::Any, ActionArity::Any},
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

MenuContext folderTargetContext(MenuSite site)
{
    MenuContext ctx;
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
        case MenuAction::Rename:
            return "rename";
        case MenuAction::MoveToRubbish:
            return "moveToRubbish";
    }
    return "";
}
