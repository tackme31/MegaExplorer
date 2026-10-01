#include "PluginHostApi.h"

#include "core/IMegaClient.h"

#include <QJsonArray>

#include <algorithm>
#include <utility>

namespace
{
constexpr int kInvalidParams = -32602;
constexpr int kMethodNotFound = -32601;
constexpr int kDefaultPageSize = 500;
constexpr int kMaxPageSize = 1000;

PluginHostApi::Reply ok(QJsonValue result)
{
    return {std::move(result), std::nullopt, {}};
}

PluginHostApi::Reply fail(int code, const QString& message)
{
    return {{}, code, message};
}

QString typeOf(const NodeSnapshot& node)
{
    return node.isFolder ? QStringLiteral("folder") : QStringLiteral("file");
}
} // namespace

PluginHostApi::PluginHostApi(std::shared_ptr<IMegaClient> client) : mClient(std::move(client)) {}

PluginHostApi::Reply PluginHostApi::call(const QString& method, const QJsonObject& params) const
{
    if (method == QStringLiteral("items.get"))
        return itemsGet(params);
    if (method == QStringLiteral("items.children"))
        return itemsChildren(params);
    return fail(kMethodNotFound, QStringLiteral("Method not found: %1").arg(method));
}

std::optional<QJsonObject> PluginHostApi::itemRef(std::uint64_t handle) const
{
    const Result<NodeSnapshot> node = mClient->getNodeSnapshot(handle);
    if (!node.success)
        return std::nullopt;
    const NodeSnapshot& n = node.value();
    return QJsonObject{
        {QStringLiteral("handle"), QString::fromStdString(mClient->handleToBase64(n.handle))},
        {QStringLiteral("name"), QString::fromStdString(n.name)},
        {QStringLiteral("type"), typeOf(n)},
        {QStringLiteral("parent"),
         n.hasParent ? QJsonValue(QString::fromStdString(mClient->handleToBase64(n.parentHandle)))
                     : QJsonValue(QJsonValue::Null)}};
}

namespace
{
QJsonObject toItem(const IMegaClient& client, const NodeSnapshot& n)
{
    QJsonArray tags;
    for (const std::string& tag : n.tags)
        tags.append(QString::fromStdString(tag));
    return QJsonObject{
        {QStringLiteral("handle"), QString::fromStdString(client.handleToBase64(n.handle))},
        {QStringLiteral("name"), QString::fromStdString(n.name)},
        {QStringLiteral("type"), typeOf(n)},
        {QStringLiteral("parent"),
         n.hasParent ? QJsonValue(QString::fromStdString(client.handleToBase64(n.parentHandle)))
                     : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("size"), static_cast<double>(n.sizeBytes)},
        {QStringLiteral("mtime"), static_cast<double>(n.modificationTime)},
        {QStringLiteral("path"), QString::fromStdString(n.path)},
        {QStringLiteral("favourite"), n.isFavourite},
        {QStringLiteral("description"), QString::fromStdString(n.description)},
        {QStringLiteral("tags"), tags}};
}

// value as a node handle, or the error reply to send instead.
std::optional<PluginHostApi::Reply>
decodeHandle(const IMegaClient& client, const QJsonValue& value, std::uint64_t* handle)
{
    if (!value.isString())
        return fail(kInvalidParams, QStringLiteral("a handle must be a string"));
    const Result<std::uint64_t> decoded = client.base64ToHandle(value.toString().toStdString());
    if (!decoded.success)
        return fail(kInvalidParams, QStringLiteral("\"%1\" is not a handle").arg(value.toString()));
    *handle = decoded.value();
    return std::nullopt;
}

std::optional<PluginHostApi::Reply>
readHandle(const IMegaClient& client, const QJsonObject& params, std::uint64_t* handle)
{
    return decodeHandle(client, params.value(QStringLiteral("handle")), handle);
}
} // namespace

PluginHostApi::Reply PluginHostApi::itemsGet(const QJsonObject& params) const
{
    const QJsonValue handles = params.value(QStringLiteral("handles"));
    if (!handles.isArray())
        return fail(kInvalidParams, QStringLiteral("\"handles\" must be an array"));
    // All or nothing: one missing node fails the call, naming it.
    QJsonArray items;
    for (const QJsonValue value : handles.toArray())
    {
        std::uint64_t handle = 0;
        if (std::optional<Reply> error = decodeHandle(*mClient, value, &handle))
            return *error;
        const Result<NodeSnapshot> node = mClient->getNodeSnapshot(handle);
        if (!node.success)
            return fail(kItemNotFound, QStringLiteral("No such item: %1").arg(value.toString()));
        items.append(toItem(*mClient, node.value()));
    }
    return ok(QJsonObject{{QStringLiteral("items"), items}});
}

PluginHostApi::Reply PluginHostApi::itemsChildren(const QJsonObject& params) const
{
    std::uint64_t handle = 0;
    if (std::optional<Reply> error = readHandle(*mClient, params, &handle))
        return *error;

    const QJsonValue type = params.value(QStringLiteral("type"));
    const bool anyType = type.isUndefined() || type.isNull();
    if (!anyType && type != QStringLiteral("file") && type != QStringLiteral("folder"))
        return fail(kInvalidParams, QStringLiteral("\"type\" must be \"file\" or \"folder\""));
    const bool wantFolders = type == QStringLiteral("folder");

    // The cursor is an offset into the (type-filtered) children, opaque to the plugin.
    int offset = 0;
    const QJsonValue cursor = params.value(QStringLiteral("cursor"));
    if (cursor.isString())
    {
        bool parsed = false;
        offset = cursor.toString().toInt(&parsed);
        if (!parsed || offset < 0)
            return fail(kInvalidParams, QStringLiteral("Bad cursor"));
    }
    else if (!cursor.isUndefined() && !cursor.isNull())
    {
        return fail(kInvalidParams, QStringLiteral("\"cursor\" must be a string"));
    }
    const int limit =
        std::clamp(params.value(QStringLiteral("limit")).toInt(kDefaultPageSize), 1, kMaxPageSize);

    if (!mClient->getNodeSnapshot(handle).success)
        return fail(kItemNotFound, QStringLiteral("No such item"));
    const Result<std::vector<NodeSnapshot>> children = mClient->getChildSnapshots(handle);
    if (!children.success)
        return fail(kInvalidParams, QStringLiteral("Not a folder"));

    std::vector<const NodeSnapshot*> matching;
    for (const NodeSnapshot& child : children.value())
    {
        if (anyType || child.isFolder == wantFolders)
            matching.push_back(&child);
    }
    const std::size_t begin = std::min(static_cast<std::size_t>(offset), matching.size());
    const std::size_t end = std::min(begin + static_cast<std::size_t>(limit), matching.size());
    QJsonArray items;
    for (std::size_t i = begin; i < end; ++i)
        items.append(toItem(*mClient, *matching[i]));
    const QJsonValue next =
        end < matching.size() ? QJsonValue(QString::number(end)) : QJsonValue(QJsonValue::Null);
    return ok(QJsonObject{{QStringLiteral("items"), items}, {QStringLiteral("nextCursor"), next}});
}
