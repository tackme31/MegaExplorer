#include "NodeDetailsService.h"

#include "PathSegment.h"

#include <utility>
#include <vector>

NodeDetailsService::NodeDetailsService(std::shared_ptr<IMegaClient> client)
    : mClient(std::move(client))
{}

void NodeDetailsService::loadDetails(std::uint64_t handle,
                                     bool isFolder,
                                     std::function<void(Result<NodeDetails>)> onDone)
{
    // The client is captured rather than `this` so the chain needs no part of this
    // object once it starts (same shape as LocalLinkService). That capture dies with
    // the in-stack getPath call, so the inner lambda -- the one that really runs on
    // an SDK thread -- deliberately holds no client: anything added there that calls
    // back into one needs its own keep-alive.
    //
    // isRoot false, like FolderNavigationService::resolvePathOf: a caller here names
    // a node it saw in a listing, and no listing contains a root.
    mClient->getPath(
        handle,
        false,
        [client = mClient, handle, isFolder, onDone = std::move(onDone)](
            Result<std::vector<PathSegment>> pathResult) {
            if (!pathResult.success)
            {
                onDone(Result<NodeDetails>::fail(std::move(pathResult.errorMessage),
                                                 pathResult.errorCode));
                return;
            }

            const std::vector<PathSegment>& segments = pathResult.value();
            NodeDetails details;
            if (!segments.empty())
                details.rootKind = segments.front().kind;
            // [1, size-1): segment 0 is the nameless root sentinel and the last is
            // the node itself, so what remains is exactly the folder chain it sits in.
            for (std::size_t i = 1; i + 1 < segments.size(); ++i)
            {
                details.parentPath += '/';
                details.parentPath += segments[i].name;
            }

            if (!isFolder)
            {
                onDone(Result<NodeDetails>::ok(std::move(details)));
                return;
            }

            client->getFolderInfo(
                handle,
                false,
                [details = std::move(details), onDone](Result<FolderInfo> infoResult) mutable {
                    if (!infoResult.success)
                    {
                        onDone(Result<NodeDetails>::fail(std::move(infoResult.errorMessage),
                                                         infoResult.errorCode));
                        return;
                    }
                    details.contents = infoResult.value();
                    details.hasContents = true;
                    onDone(Result<NodeDetails>::ok(std::move(details)));
                });
        });
}
