#pragma once
#include "core/IPinnedFolderStore.h"

// Stores the pin list as a single JSON array string under QSettings, nested
// per account at "quickAccess/accounts/<accountKey>/pinnedFolders" (Phase
// 11a) -- distinct from the QML Settings item's own default "General" group
// in Main.qml, so the two never collide. Keeping it in QSettings rather than
// a dedicated file -- unlike WindowsSessionStore's session.dat -- follows
// docs/PROGRESS.md's Phase 11 note that quick access persists "via Settings
// (QtCore)"; the port exists so that QuickAccessService itself stays Qt-free
// and testable.
//
// Phase 11a nests the key by accountKey (IPinnedFolderStore's doc comment)
// because Phase 11's original flat "quickAccess/pinnedFolders" key was
// machine-wide, not per-account: switching accounts silently read and then
// overwrote the previous account's pins. Pre-existing flat-key data is not
// migrated -- deliberately abandoned/orphaned in the registry, per
// docs/PROGRESS.md's Phase 11a log.
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

    Result<std::vector<PinnedFolder>> load(const std::string& accountKey) const override;
    Result<void> save(const std::string& accountKey,
                      const std::vector<PinnedFolder>& pins) override;
};
