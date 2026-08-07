#pragma once
#include "core/IPinnedFolderStore.h"

#include <string>

// Stores the pin list as one JSON array string under QSettings, nested per account
// at "quickAccess/accounts/<accountKey>/pinnedFolders" -- distinct from the QML
// Settings item's own "General" group, so the two never collide. The port exists so
// QuickAccessService itself stays Qt-free and testable.
//
// The key is nested by account because a flat one is machine-wide: switching
// accounts silently read and then overwrote the previous account's pins. Data left
// under the old flat key is deliberately not migrated.
//
// Depends on QCoreApplication::setOrganizationName/setApplicationName having run.
//
// A single JSON string rather than QSettings' beginWriteArray, deliberately: an
// array write leaves a "size" key plus a subgroup per element behind, and shrinking
// the list needs a remove() first or stale indices survive.
class QSettingsPinnedFolderStore : public IPinnedFolderStore
{
public:
    // Empty iniFilePath means QSettings' own scoped store, the registry on Windows.
    // A path confines it to one INI file, which is how tests exercise the real
    // adapter without writing to the user's registry; production passes none.
    explicit QSettingsPinnedFolderStore(std::string iniFilePath = {});
    ~QSettingsPinnedFolderStore() override;

    QSettingsPinnedFolderStore(const QSettingsPinnedFolderStore&) = delete;
    QSettingsPinnedFolderStore& operator=(const QSettingsPinnedFolderStore&) = delete;

    Result<std::vector<PinnedFolder>> load(const std::string& accountKey) const override;
    Result<void> save(const std::string& accountKey,
                      const std::vector<PinnedFolder>& pins) override;

private:
    std::string mIniFilePath;
};
