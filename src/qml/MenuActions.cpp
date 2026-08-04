#include "MenuActions.h"

#include "core/MenuActionResolver.h"

#include <optional>

namespace
{

std::optional<MenuSite> toMenuSite(MenuActions::Site site)
{
    switch (site)
    {
        case MenuActions::FolderBackground:
            return MenuSite::FolderBackground;
        case MenuActions::FolderRow:
            return MenuSite::FolderRow;
    }
    return std::nullopt;
}

} // namespace

MenuActions::MenuActions(QObject* parent) : QObject(parent) {}

QStringList MenuActions::forSite(Site site) const
{
    const std::optional<MenuSite> resolved = toMenuSite(site);
    if (!resolved)
        return {};

    QStringList actions;
    for (MenuAction action : resolveMenuActions(folderTargetContext(*resolved)))
        actions.append(QString::fromLatin1(menuActionId(action)));
    return actions;
}
