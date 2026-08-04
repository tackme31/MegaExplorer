#include "MegaSdkClient.h"

#include "core/MegaErrorCodes.h"
#include "MegaSdkLogger.h"

#include <algorithm>
#include <megaapi.h>
#include <utility>

// Keeps MegaErrorCodes.h's mirror in sync with the real SDK values -- this
// is the only file that can see both headers, since src/core/src/qml can't
// include megaapi.h.
static_assert(MegaErrorCode::kEArgs == mega::MegaError::API_EARGS, "MegaErrorCodes.h out of sync");
static_assert(MegaErrorCode::kEAgain == mega::MegaError::API_EAGAIN,
              "MegaErrorCodes.h out of sync");
static_assert(MegaErrorCode::kEFailed == mega::MegaError::API_EFAILED,
              "MegaErrorCodes.h out of sync");
static_assert(MegaErrorCode::kETooMany == mega::MegaError::API_ETOOMANY,
              "MegaErrorCodes.h out of sync");
static_assert(MegaErrorCode::kEExpired == mega::MegaError::API_EEXPIRED,
              "MegaErrorCodes.h out of sync");
static_assert(MegaErrorCode::kECircular == mega::MegaError::API_ECIRCULAR,
              "MegaErrorCodes.h out of sync");
static_assert(MegaErrorCode::kENoEnt == mega::MegaError::API_ENOENT,
              "MegaErrorCodes.h out of sync");
static_assert(MegaErrorCode::kEAccess == mega::MegaError::API_EACCESS,
              "MegaErrorCodes.h out of sync");
static_assert(MegaErrorCode::kEExist == mega::MegaError::API_EEXIST,
              "MegaErrorCodes.h out of sync");
static_assert(MegaErrorCode::kESid == mega::MegaError::API_ESID, "MegaErrorCodes.h out of sync");
static_assert(MegaErrorCode::kEBlocked == mega::MegaError::API_EBLOCKED,
              "MegaErrorCodes.h out of sync");
static_assert(MegaErrorCode::kEMfaRequired == mega::MegaError::API_EMFAREQUIRED,
              "MegaErrorCodes.h out of sync");

namespace
{

// Shared by every SDK call whose completion is a bare success/failure with
// no extra payload (login, loginWithSession, multiFactorAuthLogin, logout)
// -- LoginListener and FetchNodesListener used to duplicate this verbatim
// before being merged here. fetchNodes has since moved back out to its own
// FetchNodesListener below, because it is the one request type that also
// reports progress and the other four must not pay for that.
class SimpleResultListener : public mega::MegaRequestListener
{
public:
    explicit SimpleResultListener(std::function<void(Result<void>)> onDone)
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

// SimpleResultListener plus onRequestUpdate, which the SDK documents as
// firing for TYPE_FETCH_NODES only (megaapi.h:9261) and which reports the
// `f` response's HTTP download progress. See IMegaClient::fetchNodes for the
// three ways that progress is narrower than it looks; nothing here tries to
// smooth over them, they are the caller's to handle.
class FetchNodesListener : public mega::MegaRequestListener
{
public:
    FetchNodesListener(std::function<void(std::uint64_t, std::uint64_t)> onProgress,
                       std::function<void(Result<void>)> onDone)
        : mOnProgress(std::move(onProgress)), mOnDone(std::move(onDone))
    {}

    void onRequestUpdate(mega::MegaApi* /*api*/, mega::MegaRequest* request) override
    {
        // Both getters return long long and both can still be at their -1
        // default here: the SDK only calls setTotalBytes once the response
        // length is known (megaapi_impl.cpp:16150), so early updates can
        // carry an unknown total. Casting -1 straight to uint64_t would hand
        // the UI 1.8e19 and pin its bar at zero forever.
        const long long transferred = request->getTransferredBytes();
        const long long total = request->getTotalBytes();
        mOnProgress(transferred > 0 ? static_cast<std::uint64_t>(transferred) : 0,
                    total > 0 ? static_cast<std::uint64_t>(total) : 0);
    }

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
    std::function<void(std::uint64_t, std::uint64_t)> mOnProgress;
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

FileEntry nodeToEntry(mega::MegaNode* node)
{
    FileEntry entry;
    entry.name = node->getName() ? node->getName() : "";
    entry.handle = node->getHandle();
    entry.sizeBytes = node->isFile() ? static_cast<std::uint64_t>(node->getSize()) : 0;
    entry.isFolder = node->isFolder();
    entry.modificationTime = node->getModificationTime();
    entry.hasThumbnail = node->hasThumbnail();
    return entry;
}

std::vector<FileEntry> nodeListToEntries(mega::MegaNodeList* children)
{
    std::vector<FileEntry> entries;
    entries.reserve(children ? static_cast<size_t>(children->size()) : 0);
    if (children)
    {
        for (int i = 0; i < children->size(); ++i)
        {
            // owned by the list, do not delete
            entries.push_back(nodeToEntry(children->get(i)));
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

// Same shape as DownloadListener, minus the alreadyPresent inference (see
// UploadOutcome.h for why there is no upload equivalent of it).
class UploadListener : public mega::MegaTransferListener
{
public:
    UploadListener(std::function<void(std::uint64_t, std::uint64_t)> onProgress,
                   std::function<void(Result<UploadOutcome>)> onDone,
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
            UploadOutcome outcome;
            outcome.nodeHandle = static_cast<std::uint64_t>(transfer->getNodeHandle());
            mOnDone(Result<UploadOutcome>::ok(std::move(outcome)));
        }
        else
        {
            mOnDone(Result<UploadOutcome>::fail(e->getErrorString(), code));
        }
        delete this; // also destroys mCancelToken -- SDK requires it alive until here
    }

private:
    std::function<void(std::uint64_t, std::uint64_t)> mOnProgress;
    std::function<void(Result<UploadOutcome>)> mOnDone;
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
    mApi->login(email.c_str(), password.c_str(), new SimpleResultListener(std::move(onDone)));
}

void MegaSdkClient::loginWithSession(const std::string& sessionToken,
                                     std::function<void(Result<void>)> onDone)
{
    mApi->fastLogin(sessionToken.c_str(), new SimpleResultListener(std::move(onDone)));
}

void MegaSdkClient::multiFactorAuthLogin(const std::string& email,
                                         const std::string& password,
                                         const std::string& pin,
                                         std::function<void(Result<void>)> onDone)
{
    mApi->multiFactorAuthLogin(
        email.c_str(), password.c_str(), pin.c_str(), new SimpleResultListener(std::move(onDone)));
}

void MegaSdkClient::logout(std::function<void(Result<void>)> onDone)
{
    // ENABLE_SYNC is a PUBLIC define from the SDK's own CMake (sdklib_target.cmake),
    // always on for this project -- the 2-argument overload is the only one
    // that exists here, so no #ifdef branch is needed.
    // keepSyncConfigsFile=false -- this app never uses sync.
    mApi->logout(false, new SimpleResultListener(std::move(onDone)));
}

Result<std::string> MegaSdkClient::currentSessionToken() const
{
    char* session = mApi->dumpSession(); // non-const, but callable from a const member via
                                         // unique_ptr's operator->
    if (!session)
        return Result<std::string>::fail("not logged in");
    std::string token(session);
    delete[] session; // megaapi.h: "Use delete[] to release the memory"
    return Result<std::string>::ok(std::move(token));
}

Result<std::uint64_t> MegaSdkClient::currentUserHandle() const
{
    const mega::MegaHandle handle = mApi->getMyUserHandleBinary();
    if (handle == mega::INVALID_HANDLE)
        return Result<std::uint64_t>::fail("not logged in");
    return Result<std::uint64_t>::ok(static_cast<std::uint64_t>(handle));
}

void MegaSdkClient::fetchNodes(
    std::function<void(std::uint64_t transferredBytes, std::uint64_t totalBytes)> onProgress,
    std::function<void(Result<void>)> onDone)
{
    mApi->fetchNodes(new FetchNodesListener(std::move(onProgress), std::move(onDone)));
}

void MegaSdkClient::syncPendingChanges(std::function<void(Result<void>)> onDone)
{
    mApi->catchup(new SimpleResultListener(std::move(onDone)));
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

void MegaSdkClient::upload(const std::string& localPath,
                           std::uint64_t parentHandle,
                           bool parentIsRoot,
                           std::function<void(std::uint64_t, std::uint64_t)> onProgress,
                           std::function<void(Result<UploadOutcome>)> onDone)
{
    std::unique_ptr<mega::MegaNode> parent = resolveNode(parentHandle, parentIsRoot);
    if (!parent)
    {
        onDone(Result<UploadOutcome>::fail("No destination folder with the given handle (nodes not "
                                           "fetched / folder deleted)",
                                           MegaErrorCode::kENoEnt));
        return;
    }

    std::unique_ptr<mega::MegaCancelToken> cancelToken(mega::MegaCancelToken::createInstance());
    mega::MegaCancelToken* cancelTokenRaw = cancelToken.get(); // extract before moving below
    auto* listener =
        new UploadListener(std::move(onProgress), std::move(onDone), std::move(cancelToken));

    // options == nullptr means all defaults (name taken from localPath, local
    // mtime preserved, not a temporary source) -- megaapi.cpp only copies the
    // struct when it's non-null, so there's nothing to construct here.
    mApi->startUpload(localPath, parent.get(), cancelTokenRaw, /*options*/ nullptr, listener);
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

void MegaSdkClient::getPath(std::uint64_t handle,
                            bool isRoot,
                            std::function<void(Result<std::vector<PathSegment>>)> onDone)
{
    std::unique_ptr<mega::MegaNode> node = resolveNode(handle, isRoot);
    if (!node)
    {
        onDone(Result<std::vector<PathSegment>>::fail(
            "No node with the given handle (not logged in / nodes not fetched / invalid handle)"));
        return;
    }

    std::vector<PathSegment> segments;
    while (node)
    {
        PathSegment segment;
        segment.name = node->getName() ? node->getName() : "";
        segment.handle = static_cast<std::uint64_t>(node->getHandle());
        segments.push_back(std::move(segment));
        // MegaNode has no getParentNode() of its own -- it's MegaApi's, and
        // returns NULL both for a not-found node and for an actual root node
        // (megaapi.h's doc comment on MegaApi::getParentNode).
        node = std::unique_ptr<mega::MegaNode>(mApi->getParentNode(node.get()));
    }

    std::reverse(segments.begin(), segments.end());
    segments.front().isRoot = true;
    segments.front().handle = 0;

    onDone(Result<std::vector<PathSegment>>::ok(std::move(segments)));
}

void MegaSdkClient::getNodeInfo(std::uint64_t handle, std::function<void(Result<NodeInfo>)> onDone)
{
    std::unique_ptr<mega::MegaNode> node = resolveNode(handle, false);
    if (!node)
    {
        onDone(Result<NodeInfo>::fail(
            "No node with the given handle (not logged in / nodes not fetched / node deleted)"));
        return;
    }

    NodeInfo info;
    info.name = node->getName() ? node->getName() : "";
    info.handle = static_cast<std::uint64_t>(node->getHandle());
    info.isFolder = node->isFolder();
    // A deleted node still resolves -- it just lives under the Rubbish bin
    // now -- so this is the only reliable "still usable" test.
    info.inCloud = mApi->isInCloud(node.get());

    onDone(Result<NodeInfo>::ok(std::move(info)));
}

void MegaSdkClient::renameNode(std::uint64_t handle,
                               const std::string& newName,
                               std::function<void(Result<void>)> onDone)
{
    std::unique_ptr<mega::MegaNode> node = resolveNode(handle, false);
    if (!node)
    {
        onDone(Result<void>::fail(
            "No node with the given handle (not logged in / nodes not fetched / node deleted)"));
        return;
    }

    mApi->renameNode(node.get(), newName.c_str(), new SimpleResultListener(std::move(onDone)));
}

void MegaSdkClient::moveToRubbish(std::uint64_t handle, std::function<void(Result<void>)> onDone)
{
    std::unique_ptr<mega::MegaNode> node = resolveNode(handle, false);
    if (!node)
    {
        onDone(Result<void>::fail(
            "No node with the given handle (not logged in / nodes not fetched / node deleted)"));
        return;
    }

    // Deleting in MEGA is a move into the Rubbish bin, not MegaApi::remove()
    // (which destroys the node outright) -- see IMegaClient::moveToRubbish.
    std::unique_ptr<mega::MegaNode> rubbish(mApi->getRubbishNode());
    if (!rubbish)
    {
        onDone(Result<void>::fail("Rubbish bin not available (nodes not fetched)"));
        return;
    }

    mApi->moveNode(node.get(), rubbish.get(), new SimpleResultListener(std::move(onDone)));
}

void MegaSdkClient::moveNode(std::uint64_t handle,
                             std::uint64_t newParentHandle,
                             bool newParentIsRoot,
                             std::function<void(Result<void>)> onDone)
{
    std::unique_ptr<mega::MegaNode> node = resolveNode(handle, false);
    if (!node)
    {
        onDone(Result<void>::fail("No node with the given handle (not logged in / nodes not "
                                  "fetched / node deleted)",
                                  MegaErrorCode::kENoEnt));
        return;
    }

    std::unique_ptr<mega::MegaNode> parent = resolveNode(newParentHandle, newParentIsRoot);
    if (!parent)
    {
        onDone(Result<void>::fail("No destination folder with the given handle (nodes not "
                                  "fetched / folder deleted)",
                                  MegaErrorCode::kENoEnt));
        return;
    }

    mApi->moveNode(node.get(), parent.get(), new SimpleResultListener(std::move(onDone)));
}

void MegaSdkClient::createFolder(std::uint64_t parentHandle,
                                 bool parentIsRoot,
                                 const std::string& name,
                                 std::function<void(Result<void>)> onDone)
{
    std::unique_ptr<mega::MegaNode> parent = resolveNode(parentHandle, parentIsRoot);
    if (!parent)
    {
        onDone(Result<void>::fail("No parent folder with the given handle (nodes not "
                                  "fetched / folder deleted)",
                                  MegaErrorCode::kENoEnt));
        return;
    }

    // No pre-check for an existing same-named folder: the API answers that
    // itself with API_EEXIST (see IMegaClient::createFolder).
    mApi->createFolder(name.c_str(), parent.get(), new SimpleResultListener(std::move(onDone)));
}

Result<void> MegaSdkClient::checkMove(std::uint64_t handle,
                                      std::uint64_t newParentHandle,
                                      bool newParentIsRoot) const
{
    std::unique_ptr<mega::MegaNode> node = resolveNode(handle, false);
    std::unique_ptr<mega::MegaNode> parent = resolveNode(newParentHandle, newParentIsRoot);
    if (!node || !parent)
        return Result<void>::fail("Source or destination no longer exists", MegaErrorCode::kENoEnt);

    // Stricter than the SDK, which happily accepts a move to where the node
    // already is. Detected here rather than by comparing handles in a caller
    // because this is the only layer holding real nodes -- a caller looking at
    // the root would have only the isRoot sentinel, never the root's actual
    // handle, so it couldn't make the comparison at all.
    if (node->getParentHandle() == parent->getHandle())
        return Result<void>::fail("Already in that folder", MegaErrorCode::kEArgs);

    // Ownership of the returned MegaError is the caller's (megaapi.h's
    // checkMoveErrorExtended docs), unlike the borrowed MegaError* handed to
    // MegaRequestListener::onRequestFinish.
    std::unique_ptr<mega::MegaError> error(mApi->checkMoveErrorExtended(node.get(), parent.get()));
    if (!error || error->getErrorCode() == mega::MegaError::API_OK)
        return Result<void>::ok();

    return Result<void>::fail(error->getErrorString(), error->getErrorCode());
}

Result<void> MegaSdkClient::checkUpload(std::uint64_t parentHandle, bool parentIsRoot) const
{
    std::unique_ptr<mega::MegaNode> parent = resolveNode(parentHandle, parentIsRoot);
    if (!parent)
        return Result<void>::fail("Destination folder no longer exists", MegaErrorCode::kENoEnt);

    if (!parent->isFolder())
        return Result<void>::fail("Destination is not a folder", MegaErrorCode::kEArgs);

    // A deleted folder still resolves -- it just lives under the Rubbish bin
    // now -- so this is the only reliable "still usable destination" test
    // (same rationale as NodeInfo::inCloud).
    if (!mApi->isInCloud(parent.get()))
        return Result<void>::fail("Destination folder is no longer in the Cloud Drive",
                                  MegaErrorCode::kENoEnt);

    if (mApi->getAccess(parent.get()) < mega::MegaShare::ACCESS_READWRITE)
        return Result<void>::fail("No permission to add files to that folder",
                                  MegaErrorCode::kEAccess);

    return Result<void>::ok();
}

Result<std::vector<FileEntry>> MegaSdkClient::findChildFiles(
    std::uint64_t parentHandle, bool parentIsRoot, const std::vector<std::string>& names) const
{
    std::unique_ptr<mega::MegaNode> parent = resolveNode(parentHandle, parentIsRoot);
    if (!parent)
        return Result<std::vector<FileEntry>>::fail("Destination folder no longer exists",
                                                    MegaErrorCode::kENoEnt);

    std::vector<FileEntry> hits;
    for (const std::string& name : names)
    {
        // TYPE_FILE, not getChildNode(), which prefers a same-named folder.
        std::unique_ptr<mega::MegaNode> child(
            mApi->getChildNodeOfType(parent.get(), name.c_str(), mega::MegaNode::TYPE_FILE));
        if (child)
            hits.push_back(nodeToEntry(child.get()));
    }
    return Result<std::vector<FileEntry>>::ok(std::move(hits));
}

Result<bool> MegaSdkClient::hasSubfolders(std::uint64_t handle, bool isRoot) const
{
    std::unique_ptr<mega::MegaNode> node = resolveNode(handle, isRoot);
    if (!node)
        return Result<bool>::fail("No node with the given handle", MegaErrorCode::kENoEnt);

    // getNumChildFolders rather than walking getChildren(): it counts against
    // the node tree already in memory since fetchNodes(), so this costs no
    // round-trip and no MegaNodeList allocation.
    return Result<bool>::ok(node->isFolder() && mApi->getNumChildFolders(node.get()) > 0);
}

std::unique_ptr<mega::MegaNode> MegaSdkClient::resolveNode(std::uint64_t handle, bool isRoot) const
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
