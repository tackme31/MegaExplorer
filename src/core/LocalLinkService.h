#pragma once
#include "ILocalFileSystem.h"
#include "IMegaClient.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// Maps a MEGA node onto its counterpart inside the single local folder the user has
// linked to the Cloud Drive root. The link is a naming convention and nothing more:
// nothing here (or at the moment it is configured) checks that the two trees agree,
// so the existence check on the joined path is the only validation there is.
class LocalLinkService
{
public:
    LocalLinkService(std::shared_ptr<IMegaClient> client,
                     std::shared_ptr<ILocalFileSystem> fileSystem);

    // Hands back an absolute native-separator path, for a file and a folder alike.
    // Fails when localRoot is empty, when the node's MEGA path cannot be resolved,
    // or when nothing exists at the joined path.
    void resolveLocalPath(std::uint64_t handle,
                          const std::string& localRoot,
                          std::function<void(Result<std::string>)> onDone);

private:
    std::shared_ptr<IMegaClient> mClient;
    std::shared_ptr<ILocalFileSystem> mFileSystem;
};
