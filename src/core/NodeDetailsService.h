#pragma once
#include "FolderInfo.h"
#include "IMegaClient.h"
#include "ViewKind.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// What the information dialog shows about one node beyond what its row already
// carries: where it lives, and -- for a folder -- what is inside it.
struct NodeDetails
{
    // Ancestors joined with '/', root included as the leading separator, e.g.
    // "/Photos/2026". Empty for a node sitting directly under a root. The root's
    // own name is deliberately absent: it is a synthesized sentinel with no name
    // (PathSegment), and only QML knows what to call each ViewKind.
    std::string parentPath;
    // Which root the walk ended at, so QML can name the leading separator. A binned
    // node otherwise reads exactly like a Cloud Drive one.
    ViewKind rootKind = ViewKind::CloudDrive;
    // Zeroed and meaningless for a file; hasContents says which.
    FolderInfo contents;
    bool hasContents = false;
};

// Gathers the two reads the information dialog needs that a listing row cannot
// answer: IMegaClient::getPath, and IMegaClient::getFolderInfo for a folder.
class NodeDetailsService
{
public:
    explicit NodeDetailsService(std::shared_ptr<IMegaClient> client);

    // isFolder comes from the row the user right-clicked rather than from a
    // getNodeInfo of our own: that would be a third read to learn something the
    // caller already displayed. A stale flag costs a failed folder-info request,
    // not a wrong answer -- MegaSdkClient::getFolderInfo rejects a file.
    //
    // Either read failing fails the whole call: a dialog showing a location but no
    // contents (or the reverse) is harder to read than one saying it could not
    // look. onDone may run in-stack (getPath is an in-memory query) or on an SDK
    // thread (getFolderInfo is a request).
    void loadDetails(std::uint64_t handle,
                     bool isFolder,
                     std::function<void(Result<NodeDetails>)> onDone);

private:
    std::shared_ptr<IMegaClient> mClient;
};
