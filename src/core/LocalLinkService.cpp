#include "LocalLinkService.h"

#include "MegaErrorCodes.h"

#include <utility>
#include <vector>

LocalLinkService::LocalLinkService(std::shared_ptr<IMegaClient> client,
                                   std::shared_ptr<ILocalFileSystem> fileSystem)
    : mClient(std::move(client)), mFileSystem(std::move(fileSystem))
{}

void LocalLinkService::resolveLocalPath(std::uint64_t handle,
                                        const std::string& localRoot,
                                        std::function<void(Result<std::string>)> onDone)
{
    if (localRoot.empty())
    {
        onDone(Result<std::string>::fail("No local folder is linked", MegaErrorCode::kEArgs));
        return;
    }
    // isRoot false, like FolderNavigationService::resolvePathOf: a caller here names
    // a node it saw in a listing, and no listing contains a root.
    //
    // The filesystem is captured rather than `this`: the reply arrives on an
    // SDK-internal thread, and a shared_ptr copy makes the object's lifetime a
    // non-question there.
    mClient->getPath(
        handle,
        false,
        [fileSystem = mFileSystem, localRoot, onDone = std::move(onDone)](
            Result<std::vector<PathSegment>> result) {
            if (!result.success)
            {
                onDone(Result<std::string>::fail(std::move(result.errorMessage), result.errorCode));
                return;
            }
            const std::vector<PathSegment>& segments = result.value();
            std::string path = localRoot;
            while (!path.empty() && (path.back() == '\\' || path.back() == '/'))
                path.pop_back();
            // Segment 0 is the MEGA root, which the linked folder stands
            // in for; everything below it keeps its name verbatim.
            for (std::size_t i = 1; i < segments.size(); ++i)
            {
                path += '\\';
                path += segments[i].name;
            }
            if (!fileSystem->entryFor(path))
            {
                onDone(
                    Result<std::string>::fail("Nothing exists at " + path, MegaErrorCode::kENoEnt));
                return;
            }
            onDone(Result<std::string>::ok(std::move(path)));
        });
}
