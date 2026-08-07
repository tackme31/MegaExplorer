#pragma once
#include "core/ISessionStore.h"

#include <string>

// The only file allowed to include <wincrypt.h> or call
// CryptProtectData/CryptUnprotectData directly. Outside MegaExplorerCore, since that
// layer stays platform-free; unlike the SDK adapter it has its own test, DPAPI
// needing no live account.
class WindowsSessionStore : public ISessionStore
{
public:
    // filePath is fully resolved: creating the parent directory is the caller's job.
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
