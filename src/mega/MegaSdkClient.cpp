#include "MegaSdkClient.h"

#include "MegaSdkLogger.h"

#include <megaapi.h>
#include <utility>

namespace
{

class LoginListener : public mega::MegaRequestListener
{
public:
    explicit LoginListener(std::function<void(Result<void>)> onDone) : mOnDone(std::move(onDone)) {}

    void onRequestFinish(mega::MegaApi* /*api*/,
                         mega::MegaRequest* /*request*/,
                         mega::MegaError* e) override
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
    {}

    void onRequestFinish(mega::MegaApi* /*api*/,
                         mega::MegaRequest* /*request*/,
                         mega::MegaError* e) override
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

class ThumbnailListener : public mega::MegaRequestListener
{
public:
    explicit ThumbnailListener(std::function<void(Result<std::string>)> onDone)
        : mOnDone(std::move(onDone))
    {}

    void
    onRequestFinish(mega::MegaApi* /*api*/, mega::MegaRequest* request, mega::MegaError* e) override
    {
        int code = e->getErrorCode();
        if (code == mega::MegaError::API_OK)
        {
            const char* path = request->getFile(); // destination path the SDK wrote to
            mOnDone(Result<std::string>::ok(path ? path : std::string()));
        }
        else
        {
            mOnDone(Result<std::string>::fail(e->getErrorString(), code));
        }
        delete this;
    }

private:
    std::function<void(Result<std::string>)> mOnDone;
};

int toMegaOrder(SortOrder order)
{
    switch (order.key)
    {
        case SortKey::Size:
            return order.ascending ? mega::MegaApi::ORDER_SIZE_ASC : mega::MegaApi::ORDER_SIZE_DESC;
        case SortKey::ModificationTime:
            return order.ascending ? mega::MegaApi::ORDER_MODIFICATION_ASC
                                   : mega::MegaApi::ORDER_MODIFICATION_DESC;
        case SortKey::Name:
        default:
            return order.ascending ? mega::MegaApi::ORDER_DEFAULT_ASC
                                   : mega::MegaApi::ORDER_DEFAULT_DESC;
    }
}

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
            entry.hasThumbnail = node->hasThumbnail();
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

class DownloadListener : public mega::MegaTransferListener
{
public:
    DownloadListener(std::function<void(std::uint64_t, std::uint64_t)> onProgress,
                     std::function<void(Result<DownloadOutcome>)> onDone,
                     std::unique_ptr<mega::MegaCancelToken> cancelToken)
        : mOnProgress(std::move(onProgress)), mOnDone(std::move(onDone)),
          mCancelToken(std::move(cancelToken))
    {}

    void onTransferUpdate(mega::MegaApi* /*api*/, mega::MegaTransfer* transfer) override
    {
        mOnProgress(static_cast<std::uint64_t>(transfer->getTransferredBytes()),
                    static_cast<std::uint64_t>(transfer->getTotalBytes()));
    }

    void onTransferFinish(mega::MegaApi* /*api*/,
                          mega::MegaTransfer* transfer,
                          mega::MegaError* e) override
    {
        int code = e->getErrorCode();
        if (code == mega::MegaError::API_OK)
        {
            const char* path = transfer->getPath();
            DownloadOutcome outcome;
            outcome.localPath = path ? path : std::string();
            // The SDK has no public getter for "was this collision-skipped" --
            // inferred instead from MegaApiImpl::CompleteFileDownloadBySkip
            // (third_party/sdk/src/megaapi_impl.cpp), which explicitly zeroes
            // transferredBytes when it completes a transfer by skipping an
            // identical-fingerprint file already on disk, rather than writing
            // any bytes. A genuine (possibly renamed-on-collision) download
            // always has transferredBytes == totalBytes > 0 at this point.
            outcome.alreadyPresent =
                transfer->getTransferredBytes() == 0 && transfer->getTotalBytes() > 0;
            mOnDone(Result<DownloadOutcome>::ok(std::move(outcome)));
        }
        else
        {
            mOnDone(Result<DownloadOutcome>::fail(e->getErrorString(), code));
        }
        delete this; // also destroys mCancelToken -- SDK requires it alive until here
    }

private:
    std::function<void(std::uint64_t, std::uint64_t)> mOnProgress;
    std::function<void(Result<DownloadOutcome>)> mOnDone;
    std::unique_ptr<mega::MegaCancelToken> mCancelToken;
};

} // namespace

MegaSdkClient::MegaSdkClient(std::string basePath, std::string userAgent)
    : mLogger(std::make_unique<MegaSdkLogger>()),
      mApi(std::make_unique<mega::MegaApi>(nullptr, basePath.c_str(), userAgent.c_str()))
{
    // Static: addLoggerObject/removeLoggerObject register process-wide, not
    // per-MegaApi-instance. Fine here since the app only ever constructs one
    // MegaSdkClient.
    mega::MegaApi::addLoggerObject(mLogger.get());
}

MegaSdkClient::~MegaSdkClient()
{
    mega::MegaApi::removeLoggerObject(mLogger.get());
}

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

void MegaSdkClient::getRootChildren(SortOrder order,
                                    std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    listChildren(resolveNode(0, true),
                 "No root node (not logged in / nodes not fetched)",
                 order,
                 std::move(onDone));
}

void MegaSdkClient::getChildren(std::uint64_t handle,
                                SortOrder order,
                                std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    listChildren(
        resolveNode(handle, false),
        "No node with the given handle (not logged in / nodes not fetched / invalid handle)",
        order,
        std::move(onDone));
}

void MegaSdkClient::search(std::uint64_t ancestorHandle,
                           bool isRoot,
                           const std::string& query,
                           SortOrder order,
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

    std::unique_ptr<mega::MegaNodeList> results(mApi->search(filter.get(), toMegaOrder(order)));
    onDone(Result<std::vector<FileEntry>>::ok(nodeListToEntries(results.get())));
}

void MegaSdkClient::download(std::uint64_t handle,
                             const std::string& destinationPath,
                             std::function<void(std::uint64_t, std::uint64_t)> onProgress,
                             std::function<void(Result<DownloadOutcome>)> onDone)
{
    std::unique_ptr<mega::MegaNode> node = resolveNode(handle, false);
    if (!node)
    {
        onDone(Result<DownloadOutcome>::fail(
            "No node with the given handle (not logged in / nodes not fetched / invalid handle)"));
        return;
    }

    std::unique_ptr<mega::MegaCancelToken> cancelToken(mega::MegaCancelToken::createInstance());
    mega::MegaCancelToken* cancelTokenRaw = cancelToken.get(); // extract before moving below
    auto* listener =
        new DownloadListener(std::move(onProgress), std::move(onDone), std::move(cancelToken));

    mApi->startDownload(node.get(),
                        destinationPath.c_str(),
                        /*customName*/ nullptr,
                        /*appData*/ nullptr,
                        /*startFirst*/ false,
                        cancelTokenRaw,
                        mega::MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                        mega::MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N,
                        /*undelete*/ false,
                        listener);
}

void MegaSdkClient::getThumbnail(std::uint64_t handle,
                                 const std::string& destinationPath,
                                 std::function<void(Result<std::string>)> onDone)
{
    std::unique_ptr<mega::MegaNode> node = resolveNode(handle, false);
    if (!node)
    {
        onDone(Result<std::string>::fail(
            "No node with the given handle (not logged in / nodes not fetched / invalid handle)"));
        return;
    }

    // Safe to let node die on return: getNodeAttribute copies what it needs
    // into the request before queueing it.
    mApi->getThumbnail(
        node.get(), destinationPath.c_str(), new ThumbnailListener(std::move(onDone)));
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
                                 SortOrder order,
                                 std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    if (!node)
    {
        onDone(Result<std::vector<FileEntry>>::fail(notFoundMessage));
        return;
    }

    std::unique_ptr<mega::MegaNodeList> children(mApi->getChildren(node.get(), toMegaOrder(order)));
    onDone(Result<std::vector<FileEntry>>::ok(nodeListToEntries(children.get())));
}
