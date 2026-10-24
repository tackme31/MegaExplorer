#pragma once
#include "core/IMegaClient.h"

#include <QObject>

#include <memory>

// Caps how many nodes the SDK keeps in memory. The value is owned and persisted by
// Main.qml's Settings item and pushed in here, as LocalFolderController's path is.
class NodeCacheController : public QObject
{
    Q_OBJECT
    // A node count; 0 or less means no limit.
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged)

public:
    explicit NodeCacheController(std::shared_ptr<IMegaClient> client, QObject* parent = nullptr);

    int limit() const;
    void setLimit(int limit);

signals:
    void limitChanged();

private:
    std::shared_ptr<IMegaClient> mClient;
    int mLimit = 0;
};
