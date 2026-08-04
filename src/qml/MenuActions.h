#pragma once
#include "core/MenuAction.h"

#include <QObject>
#include <QStringList>

#include <QtQml/qqmlregistration.h>

// QML-facing front for MenuActionResolver's fixed-target sites: "which action
// IDs does this menu offer, in display order". Stateless, so unlike every
// other class in src/qml it is a real QML singleton (QML_SINGLETON) the engine
// instantiates itself, rather than an instance main.cpp hands over as a
// context property.
//
// Only the sites whose target is always exactly one folder are exposed.
// MenuSite::FileSelection deliberately has no Site value here: its answer
// depends on the live selection and needs a change signal, so it goes through
// FileListModel::availableActions instead. Offering it here would silently
// hand back the answer for "one folder selected" (see
// MenuActionResolver::folderTargetContext).
//
// This resolves *which* actions exist; qml/ActionCatalog.qml resolves what
// each one is called, whether it is greyed, and what it does.
class MenuActions : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    // Unscoped on purpose, matching AuthController's enums: QML then reads
    // these as MenuActions.FolderRow with no extra scope qualifier.
    enum Site
    {
        FolderBackground,
        FolderRow,
    };
    Q_ENUM(Site)

    explicit MenuActions(QObject* parent = nullptr);

    // Stable action IDs (see MenuActionResolver::menuActionId). An
    // unrecognized site yields an empty list rather than asserting -- an empty
    // menu is a visible bug, a crash isn't recoverable.
    Q_INVOKABLE QStringList forSite(Site site) const;
};
