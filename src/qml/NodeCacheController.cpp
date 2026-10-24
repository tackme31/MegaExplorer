#include "NodeCacheController.h"

#include <cstdint>
#include <limits>
#include <utility>

NodeCacheController::NodeCacheController(std::shared_ptr<IMegaClient> client, QObject* parent)
    : QObject(parent), mClient(std::move(client))
{
}

int NodeCacheController::limit() const
{
    return mLimit;
}

void NodeCacheController::setLimit(int limit)
{
    // Applied even when unchanged: the first push from Settings can equal the initial
    // value, and the SDK's own default is no limit at all.
    mClient->setNodeCacheLimit(limit > 0 ? static_cast<std::uint64_t>(limit)
                                         : std::numeric_limits<std::uint64_t>::max());
    if (mLimit == limit)
        return;
    mLimit = limit;
    emit limitChanged();
}
