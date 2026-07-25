#include "MegaSdkClient.h"

#include <megaapi.h>

#include <utility>

namespace
{

class LoginListener : public mega::MegaRequestListener
{
public:
    explicit LoginListener(std::function<void(Result<void>)> onDone)
        : mOnDone(std::move(onDone))
    {
    }

    void onRequestFinish(mega::MegaApi* /*api*/, mega::MegaRequest* /*request*/, mega::MegaError* e) override
    {
        int code = e->getErrorCode();
        if (code == mega::MegaError::API_OK)
        {
            mOnDone(Result<void>::ok());
        }
        else
        {
            mOnDone(Result<void>::fail(e->getErrorString(), code));
        }
        delete this;
    }

private:
    std::function<void(Result<void>)> mOnDone;
};

class FetchNodesListener : public mega::MegaRequestListener
{
public:
    explicit FetchNodesListener(std::function<void(Result<void>)> onDone)
        : mOnDone(std::move(onDone))
    {
    }

    void onRequestFinish(mega::MegaApi* /*api*/, mega::MegaRequest* /*request*/, mega::MegaError* e) override
    {
        int code = e->getErrorCode();
        if (code == mega::MegaError::API_OK)
        {
            mOnDone(Result<void>::ok());
        }
        else
        {
            mOnDone(Result<void>::fail(e->getErrorString(), code));
        }
        delete this;
    }

private:
    std::function<void(Result<void>)> mOnDone;
};

std::vector<FileEntry> nodeListToEntries(mega::MegaNodeList* children)
{
    std::vector<FileEntry> entries;
    entries.reserve(children ? static_cast<size_t>(children->size()) : 0);
    if (children)
    {
        for (int i = 0; i < children->size(); ++i)
        {
            mega::MegaNode* node = children->get(i); // owned by the list, do not delete
            FileEntry entry;
            entry.name = node->getName() ? node->getName() : "";
            entry.handle = node->getHandle();
            entry.sizeBytes = node->isFile() ? static_cast<std::uint64_t>(node->getSize()) : 0;
            entry.isFolder = node->isFolder();
            entry.modificationTime = node->getModificationTime();
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

} // namespace

MegaSdkClient::MegaSdkClient(std::string basePath, std::string userAgent)
    : mApi(std::make_unique<mega::MegaApi>(nullptr, basePath.c_str(), userAgent.c_str()))
{
}

MegaSdkClient::~MegaSdkClient() = default;

void MegaSdkClient::login(const std::string& email,
                           const std::string& password,
                           std::function<void(Result<void>)> onDone)
{
    mApi->login(email.c_str(), password.c_str(), new LoginListener(std::move(onDone)));
}

void MegaSdkClient::fetchNodes(std::function<void(Result<void>)> onDone)
{
    mApi->fetchNodes(new FetchNodesListener(std::move(onDone)));
}

void MegaSdkClient::getRootChildren(std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    listChildren(resolveNode(0, true),
                 "No root node (not logged in / nodes not fetched)",
                 std::move(onDone));
}

void MegaSdkClient::getChildren(std::uint64_t handle,
                                 std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    listChildren(resolveNode(handle, false),
                 "No node with the given handle (not logged in / nodes not fetched / invalid handle)",
                 std::move(onDone));
}

void MegaSdkClient::search(std::uint64_t ancestorHandle,
                            bool isRoot,
                            const std::string& query,
                            std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    std::unique_ptr<mega::MegaNode> ancestor = resolveNode(ancestorHandle, isRoot);
    if (!ancestor)
    {
        onDone(Result<std::vector<FileEntry>>::fail(
            "No ancestor node for search (not logged in / nodes not fetched / invalid handle)"));
        return;
    }

    std::unique_ptr<mega::MegaSearchFilter> filter(mega::MegaSearchFilter::createInstance());
    filter->byName(query.c_str());
    filter->byLocationHandle(ancestor->getHandle());

    std::unique_ptr<mega::MegaNodeList> results(mApi->search(filter.get()));
    onDone(Result<std::vector<FileEntry>>::ok(nodeListToEntries(results.get())));
}

std::unique_ptr<mega::MegaNode> MegaSdkClient::resolveNode(std::uint64_t handle, bool isRoot)
{
    if (isRoot)
        return std::unique_ptr<mega::MegaNode>(mApi->getRootNode());
    return std::unique_ptr<mega::MegaNode>(
        mApi->getNodeByHandle(static_cast<mega::MegaHandle>(handle)));
}

void MegaSdkClient::listChildren(std::unique_ptr<mega::MegaNode> node,
                                  const char* notFoundMessage,
                                  std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    if (!node)
    {
        onDone(Result<std::vector<FileEntry>>::fail(notFoundMessage));
        return;
    }

    std::unique_ptr<mega::MegaNodeList> children(mApi->getChildren(node.get()));
    onDone(Result<std::vector<FileEntry>>::ok(nodeListToEntries(children.get())));
}
