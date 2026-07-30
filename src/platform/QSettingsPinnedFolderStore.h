#pragma once
#include "core/IPinnedFolderStore.h"

// Stores the pin list as a single JSON array string under QSettings' own
// "quickAccess" group, i.e. the same registry key the QML Settings item in
// Main.qml writes to (which uses the default "General" group, so the two
// never collide). Keeping it in QSettings rather than a dedicated file --
// unlike WindowsSessionStore's session.dat -- follows docs/PROGRESS.md's
// Phase 11 note that quick access persists "via Settings (QtCore)"; the port
// exists so that QuickAccessService itself stays Qt-free and testable.
//
// Not part of MegaExplorerCore (parallels WindowsSessionStore/src/mega), so
// Qt is available here. Depends on QCoreApplication::setOrganizationName/
// setApplicationName having already run, same as installLogging().
//
// A single JSON string, not QSettings' beginWriteArray, deliberately: an
// array write leaves a "size" key plus one subgroup per element behind, and
// shrinking the list requires a remove() first or stale indices survive.
class QSettingsPinnedFolderStore : public IPinnedFolderStore
{
public:
    QSettingsPinnedFolderStore();
    ~QSettingsPinnedFolderStore() override;

    QSettingsPinnedFolderStore(const QSettingsPinnedFolderStore&) = delete;
    QSettingsPinnedFolderStore& operator=(const QSettingsPinnedFolderStore&) = delete;

    Result<std::vector<PinnedFolder>> load() const override;
    Result<void> save(const std::vector<PinnedFolder>& pins) override;
};
