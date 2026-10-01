#include "PluginHostApi.h"

#include "GuiThread.h"
#include "core/IMegaClient.h"

#include <QDir>
#include <QJsonArray>

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

namespace
{
constexpr int kInvalidParams = -32602;
constexpr int kMethodNotFound = -32601;
constexpr int kInternalError = -32603;
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

PluginHostApi::PluginHostApi(std::shared_ptr<IMegaClient> client, QObject* guiContext)
    : mClient(std::move(client)), mGuiContext(guiContext)
{
}

void PluginHostApi::call(const QString& method,
                         const QJsonObject& params,
                         const QString& tempDir,
                         const Done& done) const
{
    if (method == QStringLiteral("items.get"))
        done(itemsGet(params));
    else if (method == QStringLiteral("items.children"))
        done(itemsChildren(params));
    else if (method == QStringLiteral("items.update"))
        itemsUpdate(params, done);
    else if (method == QStringLiteral("items.fetchPreview"))
        itemsFetchPreview(params, tempDir, done);
    else
        done(fail(kMethodNotFound, QStringLiteral("Method not found: %1").arg(method)));
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

namespace
{
using Step = std::function<void(std::function<void(Result<void>)>)>;

// Runs steps one after another on the GUI thread, stopping at the first failure.
struct UpdateChain : std::enable_shared_from_this<UpdateChain>
{
    std::shared_ptr<IMegaClient> client;
    QObject* guiContext = nullptr;
    std::uint64_t handle = 0;
    std::vector<Step> steps;
    std::size_t applied = 0;
    PluginHostApi::Done done;

    void next()
    {
        if (applied == steps.size())
        {
            finish();
            return;
        }
        steps[applied]([self = shared_from_this()](Result<void> result) {
            invokeOnGuiThread(self->guiContext, [self, result = std::move(result)]() {
                if (!result.success)
                {
                    PluginHostApi::Reply reply = fail(
                        PluginHostApi::kMegaError, QString::fromStdString(result.errorMessage));
                    reply.mutated = self->applied > 0;
                    self->done(reply);
                    return;
                }
                ++self->applied;
                self->next();
            });
        });
    }

    void finish()
    {
        const Result<NodeSnapshot> node = client->getNodeSnapshot(handle);
        PluginHostApi::Reply reply =
            node.success ? ok(QJsonObject{{QStringLiteral("item"), toItem(*client, node.value())}})
                         : fail(PluginHostApi::kItemNotFound, QStringLiteral("No such item"));
        reply.mutated = applied > 0;
        done(reply);
    }
};

// A tag list param as strings, or nullopt when it is not one.
std::optional<std::vector<std::string>> readTags(const QJsonValue& value)
{
    std::vector<std::string> tags;
    if (value.isUndefined() || value.isNull())
        return tags;
    if (!value.isArray())
        return std::nullopt;
    for (const QJsonValue tag : value.toArray())
    {
        // The SDK rejects ',' in a tag; checked here so nothing is half-applied.
        if (!tag.isString() || tag.toString().isEmpty() || tag.toString().contains(QLatin1Char(',')))
            return std::nullopt;
        tags.push_back(tag.toString().toStdString());
    }
    return tags;
}
} // namespace

void PluginHostApi::itemsUpdate(const QJsonObject& params, const Done& done) const
{
    std::uint64_t handle = 0;
    if (std::optional<Reply> error = readHandle(*mClient, params, &handle))
    {
        done(*error);
        return;
    }
    const Result<NodeSnapshot> current = mClient->getNodeSnapshot(handle);
    if (!current.success)
    {
        done(fail(kItemNotFound, QStringLiteral("No such item")));
        return;
    }
    const NodeSnapshot& node = current.value();

    // Every param is checked before anything is sent, and only real changes are:
    // calling update with the item's current values is a no-op, not an error.
    auto chain = std::make_shared<UpdateChain>();
    chain->client = mClient;
    chain->guiContext = mGuiContext;
    chain->handle = handle;
    chain->done = done;
    std::shared_ptr<IMegaClient> client = mClient;

    if (params.contains(QStringLiteral("name")))
    {
        const QJsonValue name = params.value(QStringLiteral("name"));
        if (!name.isString() || name.toString().isEmpty())
        {
            done(fail(kInvalidParams, QStringLiteral("\"name\" must be a non-empty string")));
            return;
        }
        const std::string value = name.toString().toStdString();
        if (value != node.name)
            chain->steps.push_back([client, handle, value](std::function<void(Result<void>)> onDone) {
                client->renameNode(handle, value, std::move(onDone));
            });
    }
    if (params.contains(QStringLiteral("description")))
    {
        const QJsonValue description = params.value(QStringLiteral("description"));
        if (!description.isString())
        {
            done(fail(kInvalidParams, QStringLiteral("\"description\" must be a string")));
            return;
        }
        const std::string value = description.toString().toStdString();
        if (value != node.description)
            chain->steps.push_back([client, handle, value](std::function<void(Result<void>)> onDone) {
                client->setNodeDescription(handle, value, std::move(onDone));
            });
    }
    if (params.contains(QStringLiteral("favourite")))
    {
        const QJsonValue favourite = params.value(QStringLiteral("favourite"));
        if (!favourite.isBool())
        {
            done(fail(kInvalidParams, QStringLiteral("\"favourite\" must be a boolean")));
            return;
        }
        const bool value = favourite.toBool();
        if (value != node.isFavourite)
            chain->steps.push_back([client, handle, value](std::function<void(Result<void>)> onDone) {
                client->setNodeFavourite(handle, value, std::move(onDone));
            });
    }
    if (params.contains(QStringLiteral("tags")))
    {
        const QJsonValue tagsParam = params.value(QStringLiteral("tags"));
        const QJsonObject tags = tagsParam.toObject();
        const std::optional<std::vector<std::string>> add = readTags(tags.value(QStringLiteral("add")));
        const std::optional<std::vector<std::string>> remove =
            readTags(tags.value(QStringLiteral("remove")));
        if (!tagsParam.isObject() || !add || !remove)
        {
            done(fail(kInvalidParams,
                      QStringLiteral("\"tags\" must be {add?, remove?} lists of non-empty tags "
                                     "without ','")));
            return;
        }
        const std::set<std::string> present(node.tags.begin(), node.tags.end());
        const std::set<std::string> removing(remove->begin(), remove->end());
        std::vector<std::string> adding;
        for (const std::string& tag : *add)
        {
            if (removing.count(tag))
            {
                done(fail(kInvalidParams,
                          QStringLiteral("tag \"%1\" is both added and removed")
                              .arg(QString::fromStdString(tag))));
                return;
            }
            if (!present.count(tag) &&
                std::find(adding.begin(), adding.end(), tag) == adding.end())
                adding.push_back(tag);
        }
        // Removes first: a replacement must free its slots and bytes before the SDK
        // checks the 10-tag and 3000-byte limits for the adds.
        for (const std::string& tag : removing)
        {
            if (present.count(tag))
                chain->steps.push_back([client, handle, tag](std::function<void(Result<void>)> onDone) {
                    client->removeNodeTag(handle, tag, std::move(onDone));
                });
        }
        for (const std::string& tag : adding)
            chain->steps.push_back([client, handle, tag](std::function<void(Result<void>)> onDone) {
                client->addNodeTag(handle, tag, std::move(onDone));
            });
    }
    chain->next();
}

void PluginHostApi::itemsFetchPreview(const QJsonObject& params,
                                      const QString& tempDir,
                                      const Done& done) const
{
    std::uint64_t handle = 0;
    if (std::optional<Reply> error = readHandle(*mClient, params, &handle))
    {
        done(*error);
        return;
    }
    const Result<NodeSnapshot> node = mClient->getNodeSnapshot(handle);
    if (!node.success)
    {
        done(fail(kItemNotFound, QStringLiteral("No such item")));
        return;
    }
    if (node.value().isFolder)
    {
        done(fail(kInvalidParams, QStringLiteral("A folder has no preview")));
        return;
    }
    if (!QDir().mkpath(tempDir))
    {
        done(fail(kInternalError, QStringLiteral("Could not create %1").arg(tempDir)));
        return;
    }
    // Hex, not the base64 handle: NTFS names are case-insensitive and base64 is not.
    const QString path = QDir::toNativeSeparators(
        QDir(tempDir).filePath(QString::number(handle, 16) + QStringLiteral(".jpg")));
    QObject* guiContext = mGuiContext;
    mClient->getPreview(
        handle, path.toStdString(), [guiContext, done, path](Result<std::string> result) {
            invokeOnGuiThread(guiContext, [done, path, success = result.success] {
                // Any failure reads as "no preview": one only exists when the uploader made it.
                done(success ? ok(QJsonObject{{QStringLiteral("path"), path}})
                             : fail(kItemNotFound, QStringLiteral("This item has no preview")));
            });
        });
}
