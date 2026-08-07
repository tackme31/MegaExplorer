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

// The callback adapters MegaSdkClient hands to MegaApi: each one turns one
// shape of SDK completion into the std::function the port declares. Only
// MegaSdkClient.cpp includes this -- it lives in a header rather than a .cpp
// of its own so that <megaapi.h> still gets compiled exactly once.
//
// These were an anonymous namespace inside MegaSdkClient.cpp until R5-7, so
// they no longer have internal linkage; the namespace name is what keeps them
// out of the global one.
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
        delete this;
    }

private:
    std::function<void(std::uint64_t, std::uint64_t)> mOnProgress;
    std::function<void(Result<DownloadOutcome>)> mOnDone;
    // Not required by the SDK: startDownload copies the token by value and
    // CancelToken is itself a shared handle (mega/types.h), so destroying this
    // mid-transfer would be harmless. It is held only as the hook a future
    // cancel(jobId) would call -- nothing in src/ calls cancel() today.
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
        delete this;
    }

private:
    std::function<void(std::uint64_t, std::uint64_t)> mOnProgress;
    std::function<void(Result<UploadOutcome>)> mOnDone;
    std::unique_ptr<mega::MegaCancelToken> mCancelToken;
};

} // namespace megasdk
