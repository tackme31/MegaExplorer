#pragma once
#include "core/IMegaClient.h"

#include <atomic>
#include <cstdint>
#include <memory>

// forward declarations; only files under src/mega need <megaapi.h>
namespace mega
{
class MegaApi;
class MegaNode;
} // namespace mega

class MegaSdkLogger;

// Only files under src/mega (this class and MegaSdkLogger) may include
// <megaapi.h> or touch mega::* types.
class MegaSdkClient : public IMegaClient
{
public:
    // basePath is where the SDK unconditionally creates its state-cache DB.
    // Deliberately has no default: it used to be "." (the launch CWD), which
    // silently moved the DB whenever the app was started from a different
    // directory, and a missed DB means re-downloading the entire node tree
    // (measured: 385s vs 0.6s on a 640k-node account).
    explicit MegaSdkClient(std::string basePath, std::string userAgent = "MegaExplorer");
    ~MegaSdkClient() override;

    // Explicit stop point for the SDK thread. Destroying MegaApi makes the SDK
    // thread fire every pending request/transfer as a failure before joining,
    // and this object is destroyed *after* every service and controller that
    // those callbacks touch (main.cpp declares it first), so waiting for the
    // destructor delivers them to freed memory. Call this while all of them are
    // still alive -- main.cpp does, right after app.exec() returns.
    //
    // Afterwards this object is inert: every method fails immediately instead of
    // touching the destroyed MegaApi. Idempotent, and the destructor calls it
    // too, so forgetting the explicit call only loses the ordering guarantee.
    void shutdown();

    void login(const std::string& email,
               const std::string& password,
               std::function<void(Result<void>)> onDone) override;

    void loginWithSession(const std::string& sessionToken,
                          std::function<void(Result<void>)> onDone) override;

    void multiFactorAuthLogin(const std::string& email,
                              const std::string& password,
                              const std::string& pin,
                              std::function<void(Result<void>)> onDone) override;

    void logout(std::function<void(Result<void>)> onDone) override;

    Result<std::string> currentSessionToken() const override;

    Result<std::uint64_t> currentUserHandle() const override;

    void fetchNodes(
        std::function<void(std::uint64_t transferredBytes, std::uint64_t totalBytes)> onProgress,
        std::function<void(Result<void>)> onDone) override;

    void syncPendingChanges(std::function<void(Result<void>)> onDone) override;

    void getRootChildren(SortOrder order,
                         std::function<void(Result<std::vector<FileEntry>>)> onDone) override;

    void getChildren(std::uint64_t handle,
                     SortOrder order,
                     std::function<void(Result<std::vector<FileEntry>>)> onDone) override;

    void search(std::uint64_t ancestorHandle,
                bool isRoot,
                const std::string& query,
                SortOrder order,
                std::function<void(Result<std::vector<FileEntry>>)> onDone) override;

    void download(
        std::uint64_t handle,
        const std::string& destinationPath,
        std::function<void(std::uint64_t transferredBytes, std::uint64_t totalBytes)> onProgress,
        std::function<void(Result<DownloadOutcome>)> onDone) override;

    void
    upload(const std::string& localPath,
           std::uint64_t parentHandle,
           bool parentIsRoot,
           std::function<void(std::uint64_t transferredBytes, std::uint64_t totalBytes)> onProgress,
           std::function<void(Result<UploadOutcome>)> onDone) override;

    void getThumbnail(std::uint64_t handle,
                      const std::string& destinationPath,
                      std::function<void(Result<std::string>)> onDone) override;

    void getPath(std::uint64_t handle,
                 bool isRoot,
                 std::function<void(Result<std::vector<PathSegment>>)> onDone) override;

    void getNodeInfo(std::uint64_t handle, std::function<void(Result<NodeInfo>)> onDone) override;

    void renameNode(std::uint64_t handle,
                    const std::string& newName,
                    std::function<void(Result<void>)> onDone) override;

    void moveToRubbish(std::uint64_t handle, std::function<void(Result<void>)> onDone) override;

    void moveNode(std::uint64_t handle,
                  std::uint64_t newParentHandle,
                  bool newParentIsRoot,
                  std::function<void(Result<void>)> onDone) override;

    void copyNode(std::uint64_t handle,
                  std::uint64_t newParentHandle,
                  bool newParentIsRoot,
                  const std::string& newName,
                  std::function<void(Result<void>)> onDone) override;

    void createFolder(std::uint64_t parentHandle,
                      bool parentIsRoot,
                      const std::string& name,
                      std::function<void(Result<void>)> onDone) override;

    Result<void> checkMove(std::uint64_t handle,
                           std::uint64_t newParentHandle,
                           bool newParentIsRoot) const override;

    Result<void> checkUpload(std::uint64_t parentHandle, bool parentIsRoot) const override;

    Result<std::vector<FileEntry>>
    findChildFiles(std::uint64_t parentHandle,
                   bool parentIsRoot,
                   const std::vector<std::string>& names) const override;

    Result<bool> hasSubfolders(std::uint64_t handle, bool isRoot) const override;

    Result<AccountIdentity> currentAccountIdentity() const override;

    void getMyAvatar(const std::string& destinationPath,
                     std::function<void(Result<std::string>)> onDone) override;

    void getMyUserAttribute(UserAttribute attribute,
                            std::function<void(Result<std::string>)> onDone) override;

    void getAccountInfo(std::function<void(Result<AccountInfo>)> onDone) override;

private:
    // Shared by getRootChildren/getChildren/search: isRoot selects
    // getRootNode(), otherwise looks handle up via getNodeByHandle().
    // const so the const checkMove() can use it -- unique_ptr::operator->()
    // is const-qualified but hands back a non-const MegaApi*, so nothing else
    // has to change (same trick currentSessionToken() already relies on).
    std::unique_ptr<mega::MegaNode> resolveNode(std::uint64_t handle, bool isRoot) const;

    void listChildren(std::unique_ptr<mega::MegaNode> node,
                      const char* notFoundMessage,
                      SortOrder order,
                      std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // Declared before mApi so it is destroyed last: the logger stays valid
    // while ~MegaApi runs, which is what makes the SDK's own teardown lines
    // reach the log at all. (It does *not* mean the logger is registered
    // before mApi can log -- registration happens in the constructor body,
    // by which point MegaApiImpl::init has already run and started the SDK
    // thread. Startup lines emitted in there are lost.)
    std::unique_ptr<MegaSdkLogger> mLogger;
    std::unique_ptr<mega::MegaApi> mApi;

    // Set before mApi is destroyed, so callbacks arriving on the SDK thread
    // during teardown see it and bail out instead of dereferencing a null
    // mApi. A mutex would be the obvious tool and is *not usable*: the SDK
    // delivers callbacks while holding its own sdkMutex, and our synchronous
    // methods take that same sdkMutex from the GUI thread, so any lock of ours
    // wrapping both sides inverts the order and deadlocks.
    std::atomic<bool> mShuttingDown{false};
};
