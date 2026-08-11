#include "MenuActions.h"

#include "core/MenuActionResolver.h"
#include "core/ViewKind.h"

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

// Compared as ints rather than switched on a cast value: casting an out-of-range int
// to a scoped enum is undefined, and this one comes from QML.
std::optional<ViewKind> toViewKind(int kind)
{
    if (kind == static_cast<int>(ViewKind::CloudDrive))
        return ViewKind::CloudDrive;
    if (kind == static_cast<int>(ViewKind::Favourites))
        return ViewKind::Favourites;
    return std::nullopt;
}

} // namespace

MenuActions::MenuActions(QObject* parent) : QObject(parent) {}

QStringList MenuActions::forSite(Site site, int kind) const
{
    const std::optional<MenuSite> resolvedSite = toMenuSite(site);
    const std::optional<ViewKind> resolvedKind = toViewKind(kind);
    if (!resolvedSite || !resolvedKind)
        return {};

    QStringList actions;
    for (MenuAction action : resolveMenuActions(folderTargetContext(*resolvedSite, *resolvedKind)))
        actions.append(QString::fromLatin1(menuActionId(action)));
    return actions;
}
