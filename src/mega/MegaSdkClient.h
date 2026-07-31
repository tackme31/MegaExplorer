#pragma once
#include "core/IMegaClient.h"

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
    explicit MegaSdkClient(std::string basePath = ".", std::string userAgent = "MegaExplorer");
    ~MegaSdkClient() override;

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

    void fetchNodes(std::function<void(Result<void>)> onDone) override;

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

    Result<void> checkMove(std::uint64_t handle,
                           std::uint64_t newParentHandle,
                           bool newParentIsRoot) const override;

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

    // Declared before mApi: constructed first / destroyed last, so the
    // logger is registered before mApi can log anything and stays valid
    // until mApi is gone.
    std::unique_ptr<MegaSdkLogger> mLogger;
    std::unique_ptr<mega::MegaApi> mApi;
};
