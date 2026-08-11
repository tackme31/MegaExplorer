#pragma once
#include "core/MenuAction.h"

#include <QObject>
#include <QStringList>

#include <QtQml/qqmlregistration.h>

// QML-facing front for MenuActionResolver's fixed-target sites. Stateless, so
// unlike everything else in src/qml it is a real QML singleton the engine
// instantiates itself.
//
// Only sites whose target is always exactly one folder are exposed.
// MenuSite::FileSelection deliberately has none: its answer depends on the live
// selection and needs a change signal, so it goes through
// FileListModel::availableActions. Offering it here would silently hand back the
// answer for "one folder selected".
class MenuActions : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    // Unscoped on purpose, so QML reads these as MenuActions.FolderRow.
    enum Site
    {
        FolderBackground,
        FolderRow,
    };
    Q_ENUM(Site)

    explicit MenuActions(QObject* parent = nullptr);

    // kind is a ViewKind, passed as int for the same reason
    // FolderNavigationController::viewKind is one. No default: forgetting it is how a
    // favourites listing would end up offering "New folder".
    //
    // An unrecognized site or kind yields an empty list rather than asserting: an
    // empty menu is a visible bug, a crash isn't recoverable.
    Q_INVOKABLE QStringList forSite(Site site, int kind) const;
};
