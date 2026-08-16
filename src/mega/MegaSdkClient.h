#pragma once
#include "core/IMegaClient.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

// forward declarations; only files under src/mega need <megaapi.h>
namespace mega
{
class MegaApi;
class MegaCancelToken;
class MegaNode;
} // namespace mega

class MegaSdkLogger;

// Only files under src/mega (this class and MegaSdkLogger) may include
// <megaapi.h> or touch mega::* types.
class MegaSdkClient : public IMegaClient
{
public:
    // basePath is where the SDK unconditionally creates its state-cache DB.
    // Deliberately has no default: a relative one moves the DB whenever the app
    // starts from a different directory, and a missed DB means re-downloading the
    // whole node tree (385s vs 0.6s on a 640k-node account).
    explicit MegaSdkClient(std::string basePath, std::string userAgent = "MegaExplorer");
    ~MegaSdkClient() override;

    // Explicit stop point for the SDK thread. Destroying MegaApi fires every pending
    // request as a failure before joining, and this object outlives every service
    // and controller those callbacks touch, so leaving it to the destructor delivers
    // them to freed memory. Call it while everything is still alive.
    //
    // Afterwards this object is inert: every method fails immediately. Idempotent,
    // and the destructor calls it too, so forgetting only loses the ordering.
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

    void listFavourites(SortOrder order,
                        const std::string& nameFilter,
                        std::function<void(Result<std::vector<FileEntry>>)> onDone) override;

    void getRubbishChildren(SortOrder order,
                            std::function<void(Result<std::vector<FileEntry>>)> onDone) override;

    Result<RestoreTarget> getRestoreTarget(std::uint64_t handle) const override;

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

    void cancelDownload() override;
    void cancelUpload() override;

    void getThumbnail(std::uint64_t handle,
                      const std::string& destinationPath,
                      std::function<void(Result<std::string>)> onDone) override;

    void getPreview(std::uint64_t handle,
                    const std::string& destinationPath,
                    std::function<void(Result<std::string>)> onDone) override;

    void readFileContent(std::uint64_t handle,
                         std::uint64_t maxBytes,
                         std::function<void(Result<std::vector<char>>)> onDone) override;

    void getPath(std::uint64_t handle,
                 bool isRoot,
                 std::function<void(Result<std::vector<PathSegment>>)> onDone) override;

    void getNodeInfo(std::uint64_t handle, std::function<void(Result<NodeInfo>)> onDone) override;

    void renameNode(std::uint64_t handle,
                    const std::string& newName,
                    std::function<void(Result<void>)> onDone) override;

    void moveToRubbish(std::uint64_t handle, std::function<void(Result<void>)> onDone) override;

    void removeNode(std::uint64_t handle, std::function<void(Result<void>)> onDone) override;

    void cleanRubbishBin(std::function<void(Result<void>)> onDone) override;

    void moveNode(std::uint64_t handle,
                  std::uint64_t newParentHandle,
                  bool newParentIsRoot,
                  const std::string& newName,
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

    void setNodeFavourite(std::uint64_t handle,
                          bool favourite,
                          std::function<void(Result<void>)> onDone) override;

    Result<void> checkMove(std::uint64_t handle,
                           std::uint64_t newParentHandle,
                           bool newParentIsRoot) const override;

    Result<void> checkUpload(std::uint64_t parentHandle, bool parentIsRoot) const override;

    Result<std::vector<FileEntry>>
    findChildFiles(std::uint64_t parentHandle,
                   bool parentIsRoot,
                   const std::vector<std::string>& names) const override;

    Result<std::vector<FileEntry>>
    findChildFolders(std::uint64_t parentHandle,
                     bool parentIsRoot,
                     const std::vector<std::string>& names) const override;

    Result<bool> hasSubfolders(std::uint64_t handle, bool isRoot) const override;

    Result<AccountIdentity> currentAccountIdentity() const override;

    void getMyAvatar(const std::string& destinationPath,
                     std::function<void(Result<std::string>)> onDone) override;

    void getMyUserAttribute(UserAttribute attribute,
                            std::function<void(Result<std::string>)> onDone) override;

    void getFileVersioningEnabled(std::function<void(Result<bool>)> onDone) override;

    void getAccountInfo(std::function<void(Result<AccountInfo>)> onDone) override;

private:
    // const so the const checkMove() can use it: unique_ptr::operator->() is
    // const-qualified but hands back a non-const MegaApi*.
    std::unique_ptr<mega::MegaNode> resolveNode(std::uint64_t handle, bool isRoot) const;

    // nodeType is a mega::MegaNode::TYPE_* value; the header the interface lives
    // in must not include the SDK's, so the public pair spells it out instead.
    Result<std::vector<FileEntry>> findChildrenOfType(std::uint64_t parentHandle,
                                                      bool parentIsRoot,
                                                      const std::vector<std::string>& names,
                                                      int nodeType) const;

    void listChildren(std::unique_ptr<mega::MegaNode> node,
                      const char* notFoundMessage,
                      SortOrder order,
                      std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // Declared before mApi so it is destroyed last, which is what lets the SDK's own
    // teardown lines reach the log. It does not help at startup: registration happens
    // in the constructor body, after MegaApiImpl::init has already logged.
    std::unique_ptr<MegaSdkLogger> mLogger;
    std::unique_ptr<mega::MegaApi> mApi;

    // Set before mApi is destroyed, so teardown callbacks on the SDK thread bail out
    // instead of dereferencing null. A mutex is *not usable* here: the SDK delivers
    // callbacks holding its own sdkMutex, which our synchronous methods take from the
    // GUI thread, so any lock wrapping both sides inverts the order and deadlocks.
    std::atomic<bool> mShuttingDown{false};

    // The token handed to the transfer that startDownload/startUpload most recently
    // began, kept so cancelDownload()/cancelUpload() have something to set. Owned
    // here rather than by the listener because the listener deletes itself on the SDK
    // thread the moment its transfer ends -- a pointer to it could not be held
    // safely. Keeping a *finished* transfer's token instead is harmless: startDownload
    // copies the token by value and MegaCancelToken is a shared handle, so cancelling
    // a token nothing is watching does nothing.
    //
    // A mutex is usable here, unlike around mShuttingDown, only because nothing
    // under it re-enters the SDK: MegaCancelToken::cancel() just writes a shared
    // flag (mega/types.h's CancelToken) and takes no lock, so this one is a leaf and
    // cannot invert against sdkMutex. Keep it that way -- download() deliberately
    // calls startDownload *outside* the guard, and it can arrive on the SDK's own
    // callback thread with sdkMutex already held.
    std::mutex mCancelTokenMutex;
    std::unique_ptr<mega::MegaCancelToken> mDownloadCancelToken;
    std::unique_ptr<mega::MegaCancelToken> mUploadCancelToken;
};
