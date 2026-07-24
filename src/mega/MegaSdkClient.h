#pragma once
#include "core/IMegaClient.h"
#include <cstdint>
#include <memory>

// forward declarations; only MegaSdkClient.cpp needs <megaapi.h>
namespace mega { class MegaApi; class MegaNode; }

// Only this class (and its .cpp) may include <megaapi.h> or touch mega::* types.
class MegaSdkClient : public IMegaClient
{
public:
    explicit MegaSdkClient(std::string basePath = ".",
                           std::string userAgent = "MegaExplorer");
    ~MegaSdkClient() override;

    void login(const std::string& email,
               const std::string& password,
               std::function<void(Result<void>)> onDone) override;

    void fetchNodes(std::function<void(Result<void>)> onDone) override;

    void getRootChildren(
        std::function<void(Result<std::vector<FileEntry>>)> onDone) override;

    void getChildren(std::uint64_t handle,
                      std::function<void(Result<std::vector<FileEntry>>)> onDone) override;

private:
    void listChildren(std::unique_ptr<mega::MegaNode> node,
                       const char* notFoundMessage,
                       std::function<void(Result<std::vector<FileEntry>>)> onDone);

    std::unique_ptr<mega::MegaApi> mApi;
};
