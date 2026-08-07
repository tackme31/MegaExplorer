#include "WindowsSessionStore.h"

#include "app/Logging.h"
#include "core/MegaErrorCodes.h"

#include <filesystem>
#include <fstream>
#include <vector>

// clang-format off
// wincrypt.h depends on types (e.g. ULONG_PTR) declared by windows.h -- this
// order must not be alphabetized by a formatter.
#include <windows.h>
#include <wincrypt.h>
// clang-format on

namespace
{

// Fixed app-specific entropy passed as CryptProtectData's pOptionalEntropy,
// mirroring MEGAsync's use of entropy as a pepper against other same-user
// processes calling bare CryptUnprotectData. This is NOT a secret -- it ships
// in the binary and is compiled into every build; DPAPI's current-user scope
// (not this blob) is the actual security boundary. It only raises the bar
// against a casual "call CryptUnprotectData with no entropy" attempt from
// another process running as the same user.
constexpr unsigned char kEntropyBytes[] = {
    0x4d,
    0x45,
    0x67,
    0x58,
    0x70,
    0x6c,
    0x72,
    0x53,
    0x65,
    0x73,
    0x73,
    0x69,
    0x6f,
    0x6e,
    0x21,
    0x3f,
};

DATA_BLOB entropyBlob()
{
    DATA_BLOB blob;
    blob.pbData = const_cast<BYTE*>(kEntropyBytes);
    blob.cbData = static_cast<DWORD>(sizeof(kEntropyBytes));
    return blob;
}

} // namespace

WindowsSessionStore::WindowsSessionStore(const std::string& filePath) : mFilePath(filePath) {}

WindowsSessionStore::~WindowsSessionStore() = default;

Result<std::string> WindowsSessionStore::loadSession() const
{
    std::error_code ec;
    if (!std::filesystem::exists(mFilePath, ec))
    {
        // Absent file just means "no session stored yet" -- not a failure,
        // even if ec got set by the stat call itself.
        return Result<std::string>::ok(std::string());
    }

    std::ifstream in(mFilePath, std::ios::binary);
    if (!in)
    {
        qCWarning(lcSession) << "failed to open session file for reading:" << mFilePath.c_str();
        return Result<std::string>::fail("failed to open session file",
                                        MegaErrorCode::kEInternal);
    }
    std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    if (raw.empty())
    {
        qCWarning(lcSession) << "session file is empty:" << mFilePath.c_str();
        return Result<std::string>::fail("session file is empty", MegaErrorCode::kEInternal);
    }

    DATA_BLOB dataIn;
    dataIn.pbData = reinterpret_cast<BYTE*>(raw.data());
    dataIn.cbData = static_cast<DWORD>(raw.size());

    DATA_BLOB entropy = entropyBlob();
    DATA_BLOB dataOut{};
    const BOOL ok = CryptUnprotectData(&dataIn, nullptr, &entropy, nullptr, nullptr, 0, &dataOut);
    if (!ok)
    {
        qCWarning(lcSession) << "CryptUnprotectData failed, error"
                             << static_cast<qint64>(GetLastError());
        return Result<std::string>::fail("failed to decrypt session data",
                                        MegaErrorCode::kEInternal);
    }

    std::string result(reinterpret_cast<char*>(dataOut.pbData), dataOut.cbData);
    LocalFree(dataOut.pbData);
    return Result<std::string>::ok(std::move(result));
}

Result<void> WindowsSessionStore::saveSession(const std::string& sessionToken)
{
    DATA_BLOB dataIn;
    dataIn.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(sessionToken.data()));
    dataIn.cbData = static_cast<DWORD>(sessionToken.size());

    DATA_BLOB entropy = entropyBlob();
    DATA_BLOB dataOut{};
    const BOOL ok = CryptProtectData(&dataIn, L"", &entropy, nullptr, nullptr, 0, &dataOut);
    if (!ok)
    {
        qCWarning(lcSession) << "CryptProtectData failed, error"
                             << static_cast<qint64>(GetLastError());
        return Result<void>::fail("failed to encrypt session data", MegaErrorCode::kEInternal);
    }

    std::ofstream out(mFilePath, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        LocalFree(dataOut.pbData);
        qCWarning(lcSession) << "failed to open session file for writing:" << mFilePath.c_str();
        return Result<void>::fail("failed to open session file for writing",
                                  MegaErrorCode::kEInternal);
    }
    out.write(reinterpret_cast<const char*>(dataOut.pbData),
              static_cast<std::streamsize>(dataOut.cbData));
    const bool writeOk = static_cast<bool>(out);
    out.close();
    LocalFree(dataOut.pbData);

    if (!writeOk)
    {
        qCWarning(lcSession) << "failed to write session file:" << mFilePath.c_str();
        return Result<void>::fail("failed to write session file", MegaErrorCode::kEInternal);
    }
    return Result<void>::ok();
}

Result<void> WindowsSessionStore::clearSession()
{
    std::error_code ec;
    std::filesystem::remove(mFilePath, ec);
    // Idempotent by contract: always ok(), whether or not anything existed and even
    // if remove() itself reported an error.
    if (ec)
        qCWarning(lcSession) << "clearSession: remove reported" << ec.message().c_str();
    return Result<void>::ok();
}
