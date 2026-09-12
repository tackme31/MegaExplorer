#pragma once
#include "core/OpenWithEntry.h"

#include <QObject>
#include <QString>
#include <QVariantList>

#include <QtQml/qqmlregistration.h>
#include <vector>

class ViewerController;

// The user-registered "Open with" programs: the saved list, whether each one is
// offered for a given file, and starting one on a node's streaming URL
// (docs/investigations/STUDY_OPEN_WITH.md 3-3).
//
// The list is machine-wide rather than per-account -- it describes what is
// installed, not what is stored -- so its QSettings key sits outside the
// per-account tree QSettingsPinnedFolderStore writes into.
//
// The URL is fetched and spent inside launch(), never returned to QML, for the
// reason ViewerController::openInBrowser gives: it is a capability.
class OpenWithController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided as the openWithController context property")

    // How many IDs the menu expands MenuAction::OpenWithCustom into.
    Q_PROPERTY(int count READ count NOTIFY entriesChanged)

public:
    // Empty iniFilePath means QSettings' own scoped store, the registry on Windows;
    // a path confines it to one INI file, which is how tests exercise the real
    // persistence without writing to the user's registry. Same affordance, and same
    // reason, as QSettingsPinnedFolderStore.
    explicit OpenWithController(ViewerController* viewer,
                                QString iniFilePath = {},
                                QObject* parent = nullptr);

    int count() const;

    // Each element is {name, extensions, commandLine}, in registration order --
    // which is also menu order. Strings come back exactly as they were saved,
    // because the settings list shows them back to the user.
    Q_INVOKABLE QVariantList entries() const;

    // Replaces the whole list and writes it through. Wholesale rather than
    // per-entry add/edit/remove: the settings screen edits a selected row live, so
    // an incremental API would only be a second spelling of the same write.
    Q_INVOKABLE void setEntries(const QVariantList& entries);

    Q_INVOKABLE QString nameAt(int index) const;

    // Whether the entry is offered for this file, i.e. whether the menu row is
    // enabled -- it is shown either way (STUDY_OPEN_WITH.md 3-3).
    Q_INVOKABLE bool matchesAt(int index, const QString& fileName) const;

    // Starts the entry's program on the node's streaming URL. Answers with
    // programLaunched rather than a return value, so the toast is driven the same
    // way ViewerController::openInBrowser drives it.
    Q_INVOKABLE void launch(int index, quint64 handle);

signals:
    void entriesChanged();

    // name is the entry's display name, so the toast can say which program failed.
    // Carries no URL and no command line, for the capability reason above.
    void programLaunched(bool ok, const QString& name);

private:
    void load();
    void save() const;

    ViewerController* mViewer;
    QString mIniFilePath;
    std::vector<OpenWithEntry> mEntries;
};
