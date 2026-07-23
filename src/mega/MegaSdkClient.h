#pragma once
#include "core/IMegaClient.h"
#include <memory>

namespace mega { class MegaApi; }  // forward declaration; only MegaSdkClient.cpp needs <megaapi.h>

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

private:
    std::unique_ptr<mega::MegaApi> mApi;
};
