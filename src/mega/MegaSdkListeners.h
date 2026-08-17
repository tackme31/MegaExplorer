#pragma once
#include "core/AccountInfo.h"
#include "core/DownloadOutcome.h"
#include "core/MegaErrorCodes.h"
#include "core/Result.h"
#include "core/UploadOutcome.h"

#include <cstdint>
#include <functional>
#include <megaapi.h>
#include <memory>
#include <string>
#include <utility>

// The callback adapters MegaSdkClient hands to MegaApi: each turns one shape of SDK
// completion into the std::function the port declares. Only MegaSdkClient.cpp
// includes this -- a header rather than a .cpp of its own so <megaapi.h> still gets
// compiled exactly once.
namespace megasdk
{

// Listener lifetime, once for all seven classes below: each one is `new`ed at
// the call site, handed to MegaApi, and deletes itself from its finish
// callback. That is the only correct arrangement here -- the SDK never deletes
// listeners (megaapi_impl.cpp:18032,18192 delete the request/transfer, never
// the listener), and it always fires the finish callback, including on abort
// (:20830, :9692), so nothing is ever stranded.
//
// Do NOT "clean up" by calling removeRequestListener/removeTransferListener.
// They do not unsubscribe, they setListener(NULL) (:17895-17903), so the finish
// callback never arrives, `delete this` is never reached, and the listener
// leaks -- the exact opposite of what the call looks like it does.

// Shared by every SDK call whose completion is a bare success/failure with no extra
// payload (login, loginWithSession, multiFactorAuthLogin, logout). fetchNodes has
// its own listener below because it is the one request type that also reports
// progress, which these four must not pay for.
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

// SimpleResultListener plus onRequestUpdate, which the SDK documents as firing for
// TYPE_FETCH_NODES only (megaapi.h:9261) and which reports the `f` response's HTTP
// download progress. Nothing here smooths over the caveats in
// IMegaClient::fetchNodes -- they are the caller's to handle.
class FetchNodesListener : public mega::MegaRequestListener
{
public:
    FetchNodesListener(std::function<void(std::uint64_t, std::uint64_t)> onProgress,
                       std::function<void(Result<void>)> onDone)
        : mOnProgress(std::move(onProgress)), mOnDone(std::move(onDone))
    {}

    void onRequestUpdate(mega::MegaApi* /*api*/, mega::MegaRequest* request) override
    {
        // Both getters return long long and can still be at their -1 default: the SDK
        // only calls setTotalBytes once the response length is known, so early
        // updates carry an unknown total. Casting -1 to uint64_t would hand the UI
        // 1.8e19 and pin its bar at zero forever.
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

// Shared by getThumbnail and getMyAvatar: both are TYPE_GET_ATTR_* requests
// whose success payload is the local file the SDK wrote to.
class AttributeFileListener : public mega::MegaRequestListener
{
public:
    explicit AttributeFileListener(std::function<void(Result<std::string>)> onDone)
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

// getMyUserAttribute: a public user attribute's value arrives as text.
class TextResultListener : public mega::MegaRequestListener
{
public:
    explicit TextResultListener(std::function<void(Result<std::string>)> onDone)
        : mOnDone(std::move(onDone))
    {}

    void
    onRequestFinish(mega::MegaApi* /*api*/, mega::MegaRequest* request, mega::MegaError* e) override
    {
        int code = e->getErrorCode();
        if (code == mega::MegaError::API_OK)
        {
            const char* text = request->getText();
            mOnDone(Result<std::string>::ok(text ? text : std::string()));
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

// TextResultListener's twin for MegaRequest::TYPE_EXPORT, which parks its answer in
// getLink() rather than getText().
class LinkResultListener : public mega::MegaRequestListener
{
public:
    explicit LinkResultListener(std::function<void(Result<std::string>)> onDone)
        : mOnDone(std::move(onDone))
    {}

    void
    onRequestFinish(mega::MegaApi* /*api*/, mega::MegaRequest* request, mega::MegaError* e) override
    {
        int code = e->getErrorCode();
        if (code == mega::MegaError::API_OK)
        {
            const char* link = request->getLink();
            mOnDone(Result<std::string>::ok(link ? link : std::string()));
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

class AccountDetailsListener : public mega::MegaRequestListener
{
public:
    explicit AccountDetailsListener(std::function<void(Result<AccountInfo>)> onDone)
        : mOnDone(std::move(onDone))
    {}

    void
    onRequestFinish(mega::MegaApi* /*api*/, mega::MegaRequest* request, mega::MegaError* e) override
    {
        int code = e->getErrorCode();
        if (code == mega::MegaError::API_OK)
        {
            // getMegaAccountDetails() transfers ownership (megaapi.h).
            const std::unique_ptr<mega::MegaAccountDetails> details(
                request->getMegaAccountDetails());
            if (details)
            {
                AccountInfo info;
                info.storageUsedBytes = static_cast<std::uint64_t>(details->getStorageUsed());
                info.storageMaxBytes = static_cast<std::uint64_t>(details->getStorageMax());
                info.proLevel = details->getProLevel();
                mOnDone(Result<AccountInfo>::ok(info));
            }
            else
            {
                mOnDone(Result<AccountInfo>::fail("Account details missing from response",
                                                  MegaErrorCode::kEInternal));
            }
        }
        else
        {
            mOnDone(Result<AccountInfo>::fail(e->getErrorString(), code));
        }
        delete this;
    }

private:
    std::function<void(Result<AccountInfo>)> mOnDone;
};

class DownloadListener : public mega::MegaTransferListener
{
public:
    DownloadListener(std::function<void(std::uint64_t, std::uint64_t)> onProgress,
                     std::function<void(Result<DownloadOutcome>)> onDone)
        : mOnProgress(std::move(onProgress)), mOnDone(std::move(onDone))
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
            // The SDK has no public getter for "was this collision-skipped". Inferred
            // from MegaApiImpl::CompleteFileDownloadBySkip, which zeroes
            // transferredBytes when it completes a transfer by skipping an
            // identical-fingerprint file: a genuine download always has
            // transferredBytes == totalBytes > 0 here.
            outcome.alreadyPresent =
                transfer->getTransferredBytes() == 0 && transfer->getTotalBytes() > 0;
            mOnDone(Result<DownloadOutcome>::ok(std::move(outcome)));
        }
        else
        {
            mOnDone(Result<DownloadOutcome>::fail(e->getErrorString(), code));
        }
        delete this;
    }

private:
    std::function<void(std::uint64_t, std::uint64_t)> mOnProgress;
    std::function<void(Result<DownloadOutcome>)> mOnDone;
};

// Same shape as DownloadListener, minus the alreadyPresent inference (see
// UploadOutcome.h for why there is no upload equivalent of it).
class UploadListener : public mega::MegaTransferListener
{
public:
    UploadListener(std::function<void(std::uint64_t, std::uint64_t)> onProgress,
                   std::function<void(Result<UploadOutcome>)> onDone)
        : mOnProgress(std::move(onProgress)), mOnDone(std::move(onDone))
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
        delete this;
    }

private:
    std::function<void(std::uint64_t, std::uint64_t)> mOnProgress;
    std::function<void(Result<UploadOutcome>)> mOnDone;
};

// The one listener that collects bytes rather than a path: startStreaming hands
// them over in onTransferData and writes no file at all.
//
// No mutex around mBuffer -- the SDK serializes one transfer's callbacks, the same
// assumption DownloadListener already makes across onTransferUpdate/onTransferFinish.
class StreamingContentListener : public mega::MegaTransferListener
{
public:
    StreamingContentListener(std::uint64_t maxBytes,
                             std::function<void(Result<std::vector<char>>)> onDone)
        : mMaxBytes(maxBytes), mOnDone(std::move(onDone))
    {}

    bool onTransferData(mega::MegaApi* /*api*/,
                        mega::MegaTransfer* /*transfer*/,
                        char* buffer,
                        size_t size) override
    {
        // The SDK owns buffer and reuses it after this returns, so the copy is
        // mandatory, not an optimisation left undone.
        if (mBuffer.size() + size > mMaxBytes)
        {
            mOverflowed = true;
            return false; // aborts the transfer; the only reason we ever refuse
        }
        mBuffer.insert(mBuffer.end(), buffer, buffer + size);
        return true;
    }

    void onTransferFinish(mega::MegaApi* /*api*/,
                          mega::MegaTransfer* /*transfer*/,
                          mega::MegaError* e) override
    {
        // mOverflowed first: refusing above ends the transfer as API_EINCOMPLETE,
        // so the error code alone would report it as a network failure.
        if (mOverflowed)
        {
            mOnDone(Result<std::vector<char>>::fail("File is larger than the preview limit",
                                                    MegaErrorCode::kETooMany));
        }
        else if (e->getErrorCode() == mega::MegaError::API_OK)
        {
            mOnDone(Result<std::vector<char>>::ok(std::move(mBuffer)));
        }
        else
        {
            mOnDone(Result<std::vector<char>>::fail(e->getErrorString(), e->getErrorCode()));
        }
        delete this;
    }

private:
    std::uint64_t mMaxBytes;
    std::function<void(Result<std::vector<char>>)> mOnDone;
    std::vector<char> mBuffer;
    bool mOverflowed = false;
};

} // namespace megasdk
