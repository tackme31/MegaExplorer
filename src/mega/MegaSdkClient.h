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

    void fetchNodes(std::function<void(Result<void>)> onDone) override;

    void getRootChildren(std::function<void(Result<std::vector<FileEntry>>)> onDone) override;

    void getChildren(std::uint64_t handle,
                     std::function<void(Result<std::vector<FileEntry>>)> onDone) override;

    void search(std::uint64_t ancestorHandle,
                bool isRoot,
                const std::string& query,
                std::function<void(Result<std::vector<FileEntry>>)> onDone) override;

    void download(
        std::uint64_t handle,
        const std::string& destinationPath,
        std::function<void(std::uint64_t transferredBytes, std::uint64_t totalBytes)> onProgress,
        std::function<void(Result<DownloadOutcome>)> onDone) override;

    void getThumbnail(std::uint64_t handle,
                      const std::string& destinationPath,
                      std::function<void(Result<std::string>)> onDone) override;

private:
    // Shared by getRootChildren/getChildren/search: isRoot selects
    // getRootNode(), otherwise looks handle up via getNodeByHandle().
    std::unique_ptr<mega::MegaNode> resolveNode(std::uint64_t handle, bool isRoot);

    void listChildren(std::unique_ptr<mega::MegaNode> node,
                      const char* notFoundMessage,
                      std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // Declared before mApi: constructed first / destroyed last, so the
    // logger is registered before mApi can log anything and stays valid
    // until mApi is gone.
    std::unique_ptr<MegaSdkLogger> mLogger;
    std::unique_ptr<mega::MegaApi> mApi;
};
