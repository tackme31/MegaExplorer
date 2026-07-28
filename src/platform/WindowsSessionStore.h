#pragma once
#include "core/ISessionStore.h"

#include <string>

// The only file allowed to include <wincrypt.h> or call
// CryptProtectData/CryptUnprotectData/LocalFree directly, mirroring SqliteNodeCache's
// exclusivity over <sqlite3.h>. Windows-only for now (docs/MEMO.md). Not part of
// MegaExplorerCore (parallels SqliteNodeCache/src/mega); gets its own adapter-level
// test (tests/WindowsSessionStoreTest.cpp) since DPAPI needs no live account/network.
class WindowsSessionStore : public ISessionStore
{
public:
    // filePath: a fully resolved file path -- resolving it (QStandardPaths, mkpath
    // the parent dir) is the caller's job, same contract as SqliteNodeCache's dbPath.
    explicit WindowsSessionStore(const std::string& filePath);
    ~WindowsSessionStore() override;

    WindowsSessionStore(const WindowsSessionStore&) = delete;
    WindowsSessionStore& operator=(const WindowsSessionStore&) = delete;

    Result<std::string> loadSession() const override;
    Result<void> saveSession(const std::string& sessionToken) override;
    Result<void> clearSession() override;

private:
    std::string mFilePath;
};
