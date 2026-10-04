#include "PluginHostApi.h"

#include "GuiThread.h"
#include "core/IMegaClient.h"

#include "core/DownloadService.h"
#include "core/FileOperationService.h"
#include "core/MegaErrorCodes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
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

// An Item's fields besides handle, which is always sent.
enum ItemField : unsigned
{
    kFieldName = 1u << 0,
    kFieldType = 1u << 1,
    kFieldParent = 1u << 2,
    kFieldSize = 1u << 3,
    kFieldMtime = 1u << 4,
    kFieldPath = 1u << 5,
    kFieldFavourite = 1u << 6,
    kFieldDescription = 1u << 7,
    kFieldTags = 1u << 8,
    kFieldCrc = 1u << 9,
    kAllItemFields = (1u << 10) - 1,
};

constexpr std::pair<const char*, unsigned> kItemFieldNames[] = {
    {"name", kFieldName},
    {"type", kFieldType},
    {"parent", kFieldParent},
    {"size", kFieldSize},
    {"mtime", kFieldMtime},
    {"path", kFieldPath},
    {"favourite", kFieldFavourite},
    {"description", kFieldDescription},
    {"tags", kFieldTags},
    {"crc", kFieldCrc},
};

QJsonObject toItem(const IMegaClient& client, const NodeSnapshot& n, unsigned fields = kAllItemFields)
{
    QJsonObject item{{QStringLiteral("handle"), QString::fromStdString(client.handleToBase64(n.handle))}};
    if (fields & kFieldName)
        item.insert(QStringLiteral("name"), QString::fromStdString(n.name));
    if (fields & kFieldType)
        item.insert(QStringLiteral("type"), typeOf(n));
    if (fields & kFieldParent)
        item.insert(QStringLiteral("parent"),
                    n.hasParent ? QJsonValue(QString::fromStdString(client.handleToBase64(n.parentHandle)))
                                : QJsonValue(QJsonValue::Null));
    if (fields & kFieldSize)
        item.insert(QStringLiteral("size"), static_cast<double>(n.sizeBytes));
    if (fields & kFieldMtime)
        item.insert(QStringLiteral("mtime"), static_cast<double>(n.modificationTime));
    if (fields & kFieldPath)
        item.insert(QStringLiteral("path"), QString::fromStdString(n.path));
    if (fields & kFieldFavourite)
        item.insert(QStringLiteral("favourite"), n.isFavourite);
    if (fields & kFieldDescription)
        item.insert(QStringLiteral("description"), QString::fromStdString(n.description));
    if (fields & kFieldTags)
    {
        QJsonArray tags;
        for (const std::string& tag : n.tags)
            tags.append(QString::fromStdString(tag));
        item.insert(QStringLiteral("tags"), tags);
    }
    if (fields & kFieldCrc)
        item.insert(QStringLiteral("crc"), n.crc.empty() ? QJsonValue(QJsonValue::Null)
                                                         : QJsonValue(QString::fromStdString(n.crc)));
    return item;
}

// params.fields as a mask of ItemField, or the error reply to send instead. No
// "fields" means all of them; "handle" is accepted and always sent anyway.
std::optional<PluginHostApi::Reply> readFields(const QJsonObject& params, unsigned* fields)
{
    const QJsonValue value = params.value(QStringLiteral("fields"));
    *fields = kAllItemFields;
    if (value.isUndefined() || value.isNull())
        return std::nullopt;
    if (!value.isArray())
        return fail(kInvalidParams, QStringLiteral("\"fields\" must be an array of field names"));
    *fields = 0;
    for (const QJsonValue entry : value.toArray())
    {
        const QString name = entry.toString();
        if (name == QLatin1String("handle"))
            continue;
        const auto known = std::find_if(std::begin(kItemFieldNames),
                                        std::end(kItemFieldNames),
                                        [&name](const auto& field) { return name == QLatin1String(field.first); });
        if (!entry.isString() || known == std::end(kItemFieldNames))
            return fail(kInvalidParams, QStringLiteral("Unknown field in \"fields\": %1")
                                            .arg(entry.isString() ? name : QStringLiteral("(not a string)")));
        *fields |= known->second;
    }
    return std::nullopt;
}
} // namespace

PluginHostApi::PluginHostApi(std::shared_ptr<IMegaClient> client,
                             QObject* guiContext,
                             UserDownloads downloads,
                             Reveal reveal,
                             Search search)
    : mClient(std::move(client)), mGuiContext(guiContext), mDownloads(std::move(downloads)),
      mReveal(std::move(reveal)), mSearch(std::move(search))
{
}

std::optional<QString> PluginHostApi::requiredPermission(const QString& method)
{
    static const QHash<QString, QString> permissions{
        {QStringLiteral("items.get"), QStringLiteral("items.read")},
        {QStringLiteral("items.children"), QStringLiteral("items.read")},
        {QStringLiteral("items.descendants"), QStringLiteral("items.read")},
        {QStringLiteral("items.upload"), QStringLiteral("items.write")},
        {QStringLiteral("items.createFolder"), QStringLiteral("items.write")},
        {QStringLiteral("items.copy"), QStringLiteral("items.write")},
        {QStringLiteral("items.move"), QStringLiteral("items.write")},
        {QStringLiteral("items.moveToRubbish"), QStringLiteral("items.rubbish")},
        {QStringLiteral("items.update"), QStringLiteral("items.edit")},
        {QStringLiteral("items.fetchPreview"), QStringLiteral("content.read")},
        {QStringLiteral("items.fetchFile"), QStringLiteral("content.read")},
        {QStringLiteral("items.readRange"), QStringLiteral("content.read")},
        {QStringLiteral("transfers.download"), QStringLiteral("content.download")}};
    const auto it = permissions.constFind(method);
    if (it == permissions.constEnd())
        return std::nullopt;
    return *it;
}

void PluginHostApi::call(const QString& method,
                         const QJsonObject& params,
                         RunState& run,
                         const Done& done) const
{
    if (const std::optional<QString> permission = requiredPermission(method);
        permission && !run.permissions.contains(*permission))
    {
        if (!run.denied.contains(*permission))
            run.denied << *permission;
        Reply reply = fail(kPermissionDenied,
                           QStringLiteral("%1 needs the \"%2\" permission, which plugin.json does not declare")
                               .arg(method, *permission));
        reply.errorData = QJsonObject{{QStringLiteral("permission"), *permission}};
        done(reply);
        return;
    }
    if (method == QStringLiteral("items.get"))
        done(itemsGet(params));
    else if (method == QStringLiteral("items.children"))
        done(itemsChildren(params));
    else if (method == QStringLiteral("items.descendants"))
        itemsDescendants(params, run, done);
    else if (method == QStringLiteral("items.update"))
        itemsUpdate(params, done);
    else if (method == QStringLiteral("items.fetchPreview"))
        itemsFetchPreview(params, run.tempDir, done);
    else if (method == QStringLiteral("items.fetchFile"))
        itemsFetchFile(params, run, done);
    else if (method == QStringLiteral("items.readRange"))
        itemsReadRange(params, run, done);
    else if (method == QStringLiteral("items.upload"))
        itemsUpload(params, run, done);
    else if (method == QStringLiteral("items.createFolder"))
        itemsCreateFolder(params, done);
    else if (method == QStringLiteral("items.move"))
        itemsMove(params, done);
    else if (method == QStringLiteral("items.copy"))
        itemsCopy(params, done);
    else if (method == QStringLiteral("items.moveToRubbish"))
        itemsMoveToRubbish(params, done);
    else if (method == QStringLiteral("transfers.download"))
        done(transfersDownload(params));
    else if (method == QStringLiteral("ui.reveal"))
        done(uiReveal(params));
    else if (method == QStringLiteral("ui.search"))
        done(uiSearch(params));
    else
        done(fail(kMethodNotFound, QStringLiteral("Method not found: %1").arg(method)));
}

std::optional<QJsonObject> PluginHostApi::item(std::uint64_t handle) const
{
    const Result<NodeSnapshot> node = mClient->getNodeSnapshot(handle);
    if (!node.success)
        return std::nullopt;
    return toItem(*mClient, node.value());
}

std::optional<QJsonObject> PluginHostApi::rootItem() const
{
    const Result<NodeSnapshot> node = mClient->getRootSnapshot();
    if (!node.success)
        return std::nullopt;
    return toItem(*mClient, node.value());
}

namespace
{
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

// Which node types a listing keeps: both, or only files/folders.
struct TypeFilter
{
    bool any = true;
    bool folders = false;

    bool keeps(bool isFolder) const { return any || isFolder == folders; }
};

std::optional<PluginHostApi::Reply> readType(const QJsonObject& params, TypeFilter* filter)
{
    const QJsonValue type = params.value(QStringLiteral("type"));
    filter->any = type.isUndefined() || type.isNull();
    if (!filter->any && type != QStringLiteral("file") && type != QStringLiteral("folder"))
        return fail(kInvalidParams, QStringLiteral("\"type\" must be \"file\" or \"folder\""));
    filter->folders = type == QStringLiteral("folder");
    return std::nullopt;
}

int readLimit(const QJsonObject& params)
{
    return std::clamp(params.value(QStringLiteral("limit")).toInt(kDefaultPageSize), 1, kMaxPageSize);
}

PluginHostApi::Reply page(const QJsonArray& items, const QJsonValue& nextCursor)
{
    return ok(QJsonObject{{QStringLiteral("items"), items}, {QStringLiteral("nextCursor"), nextCursor}});
}
} // namespace

PluginHostApi::Reply PluginHostApi::itemsGet(const QJsonObject& params) const
{
    const QJsonValue handles = params.value(QStringLiteral("handles"));
    if (!handles.isArray())
        return fail(kInvalidParams, QStringLiteral("\"handles\" must be an array"));
    unsigned fields = 0;
    if (std::optional<Reply> error = readFields(params, &fields))
        return *error;
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
        items.append(toItem(*mClient, node.value(), fields));
    }
    return ok(QJsonObject{{QStringLiteral("items"), items}});
}

PluginHostApi::Reply PluginHostApi::itemsChildren(const QJsonObject& params) const
{
    std::uint64_t handle = 0;
    if (std::optional<Reply> error = readHandle(*mClient, params, &handle))
        return *error;

    TypeFilter filter;
    if (std::optional<Reply> error = readType(params, &filter))
        return *error;
    unsigned fields = 0;
    if (std::optional<Reply> error = readFields(params, &fields))
        return *error;

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
    const int limit = readLimit(params);

    if (!mClient->getNodeSnapshot(handle).success)
        return fail(kItemNotFound, QStringLiteral("No such item"));
    const Result<std::vector<NodeSnapshot>> children = mClient->getChildSnapshots(handle);
    if (!children.success)
        return fail(kInvalidParams, QStringLiteral("Not a folder"));

    std::vector<const NodeSnapshot*> matching;
    for (const NodeSnapshot& child : children.value())
    {
        if (filter.keeps(child.isFolder))
            matching.push_back(&child);
    }
    const std::size_t begin = std::min(static_cast<std::size_t>(offset), matching.size());
    const std::size_t end = std::min(begin + static_cast<std::size_t>(limit), matching.size());
    QJsonArray items;
    for (std::size_t i = begin; i < end; ++i)
        items.append(toItem(*mClient, *matching[i], fields));
    return page(items, end < matching.size() ? QJsonValue(QString::number(end)) : QJsonValue(QJsonValue::Null));
}

namespace
{
// A node gone since the listing was fixed is skipped, so a page can come up short.
PluginHostApi::Reply descendantsPage(const IMegaClient& client,
                                     const std::vector<std::uint64_t>& handles,
                                     std::size_t listing,
                                     std::size_t offset,
                                     int limit,
                                     unsigned fields)
{
    const std::size_t end = std::min(offset + static_cast<std::size_t>(limit), handles.size());
    QJsonArray items;
    for (std::size_t i = offset; i < end; ++i)
    {
        const Result<NodeSnapshot> node = client.getNodeSnapshot(handles[i]);
        if (node.success)
            items.append(toItem(client, node.value(), fields));
    }
    return page(items,
                end < handles.size() ? QJsonValue(QStringLiteral("%1:%2").arg(listing).arg(end))
                                     : QJsonValue(QJsonValue::Null));
}
} // namespace

void PluginHostApi::itemsDescendants(const QJsonObject& params, RunState& run, const Done& done) const
{
    std::uint64_t handle = 0;
    if (std::optional<Reply> error = readHandle(*mClient, params, &handle))
        return done(*error);
    TypeFilter filter;
    if (std::optional<Reply> error = readType(params, &filter))
        return done(*error);
    unsigned fields = 0;
    if (std::optional<Reply> error = readFields(params, &fields))
        return done(*error);
    const int limit = readLimit(params);

    // The cursor is "<listing>:<offset>"; no cursor starts a new listing.
    const QJsonValue cursor = params.value(QStringLiteral("cursor"));
    if (cursor.isString())
    {
        const QStringList parts = cursor.toString().split(QLatin1Char(':'));
        bool listingOk = false;
        bool offsetOk = false;
        std::size_t listing = 0;
        std::size_t offset = 0;
        if (parts.size() == 2)
        {
            listing = parts[0].toULongLong(&listingOk);
            offset = parts[1].toULongLong(&offsetOk);
        }
        if (!listingOk || !offsetOk || listing >= run.listings->size() ||
            offset > (*run.listings)[listing].size())
            return done(fail(kInvalidParams, QStringLiteral("Bad cursor")));
        return done(descendantsPage(*mClient, (*run.listings)[listing], listing, offset, limit, fields));
    }
    if (!cursor.isUndefined() && !cursor.isNull())
        return done(fail(kInvalidParams, QStringLiteral("\"cursor\" must be a string")));

    mClient->listDescendants(
        handle,
        [client = mClient, listings = std::weak_ptr(run.listings), filter, limit, fields, done](
            Result<std::vector<DescendantNode>> result) {
            if (!result.success)
                return done(result.errorCode == MegaErrorCode::kENoEnt
                                ? fail(kItemNotFound, QStringLiteral("No such item"))
                            : result.errorCode == MegaErrorCode::kEArgs
                                ? fail(kInvalidParams, QStringLiteral("Not a folder"))
                                : fail(kInternalError, QString::fromStdString(result.errorMessage)));
            const std::shared_ptr<std::vector<std::vector<std::uint64_t>>> owned = listings.lock();
            if (!owned)
                return done(fail(kInternalError, QStringLiteral("The run has ended")));
            std::vector<std::uint64_t> handles;
            for (const DescendantNode& node : result.value())
            {
                if (filter.keeps(node.isFolder))
                    handles.push_back(node.handle);
            }
            owned->push_back(std::move(handles));
            done(descendantsPage(*client, owned->back(), owned->size() - 1, 0, limit, fields));
        });
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
        // The result is "current minus remove, plus add", so a tag in both lists stays.
        // Keys match the SDK's own add/remove lookup: case-insensitive, accent-sensitive.
        const auto key = [](const std::string& tag) {
            return QString::fromStdString(tag).toCaseFolded();
        };
        std::set<QString> removeKeys;
        for (const std::string& tag : *remove)
            removeKeys.insert(key(tag));
        std::set<QString> addKeys;
        for (const std::string& tag : *add)
            addKeys.insert(key(tag));
        std::set<QString> presentKeys;
        std::vector<std::string> removing;
        for (const std::string& tag : node.tags)
        {
            const QString k = key(tag);
            presentKeys.insert(k);
            if (removeKeys.count(k) && !addKeys.count(k))
                removing.push_back(tag);
        }
        std::set<QString> addedKeys;
        std::vector<std::string> adding;
        for (const std::string& tag : *add)
        {
            const QString k = key(tag);
            if (!presentKeys.count(k) && addedKeys.insert(k).second)
                adding.push_back(tag);
        }
        // Removes first: a replacement must free its slots and bytes before the SDK
        // checks the 10-tag and 3000-byte limits for the adds.
        for (const std::string& tag : removing)
            chain->steps.push_back([client, handle, tag](std::function<void(Result<void>)> onDone) {
                client->removeNodeTag(handle, tag, std::move(onDone));
            });
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

struct PluginHostApi::TransferQueue
{
    // Starts one transfer and calls finish exactly once, on the GUI thread. May set abort.
    using Job = std::function<void(const std::shared_ptr<TransferQueue>& queue, const Done& finish)>;

    std::deque<std::pair<Job, Done>> pending;
    bool busy = false;
    bool cancelled = false;
    std::function<void()> abort;
    // Each fetched file gets a folder of its own, so two fetches never collide.
    int nextSlot = 0;
};

namespace
{
using TransferQueue = PluginHostApi::TransferQueue;

constexpr std::uint64_t kMaxReadRangeBytes = 1024 * 1024;

// MegaSdkClient keys its cancel tokens by transfer id, shared with the app's own
// download and upload queues; those count up from 1, so these start far above.
std::uint64_t nextPluginTransferId()
{
    static std::atomic<std::uint64_t> next{std::uint64_t{1} << 62};
    return next++;
}

PluginHostApi::Reply cancelledReply(bool mutated = false)
{
    PluginHostApi::Reply reply = fail(PluginHostApi::kCancelled, QStringLiteral("Cancelled"));
    reply.mutated = mutated;
    return reply;
}

PluginHostApi::Reply megaFail(const std::string& message)
{
    return fail(PluginHostApi::kMegaError, QString::fromStdString(message));
}

PluginHostApi::Reply conflict(const QString& message, const QString& reason, const QString& detail = {})
{
    PluginHostApi::Reply reply = fail(PluginHostApi::kConflict, message);
    QJsonObject data{{QStringLiteral("reason"), reason}};
    if (!detail.isEmpty())
        data.insert(QStringLiteral("message"), detail);
    reply.errorData = data;
    return reply;
}

// The new node as an Item, or just its handle if the local tree has not caught up.
QJsonObject createdItem(const IMegaClient& client, std::uint64_t handle)
{
    const Result<NodeSnapshot> node = client.getNodeSnapshot(handle);
    if (node.success)
        return toItem(client, node.value());
    return {{QStringLiteral("handle"), QString::fromStdString(client.handleToBase64(handle))}};
}

std::optional<PluginHostApi::Reply>
readNode(const IMegaClient& client, const QJsonValue& value, bool wantFolder, NodeSnapshot* out)
{
    std::uint64_t handle = 0;
    if (std::optional<PluginHostApi::Reply> error = decodeHandle(client, value, &handle))
        return error;
    const Result<NodeSnapshot> node = client.getNodeSnapshot(handle);
    if (!node.success)
        return fail(PluginHostApi::kItemNotFound, QStringLiteral("No such item: %1").arg(value.toString()));
    if (node.value().isFolder != wantFolder)
        return fail(kInvalidParams,
                    wantFolder ? QStringLiteral("%1 is not a folder").arg(value.toString())
                               : QStringLiteral("%1 is not a file").arg(value.toString()));
    *out = node.value();
    return std::nullopt;
}

// One of choices, the first being the default.
std::optional<PluginHostApi::Reply>
readChoice(const QJsonObject& params, const QString& key, const QStringList& choices, QString* out)
{
    const QJsonValue value = params.value(key);
    if (value.isUndefined() || value.isNull())
    {
        *out = choices.first();
        return std::nullopt;
    }
    if (!value.isString() || !choices.contains(value.toString()))
        return fail(kInvalidParams, QStringLiteral("%1 must be one of: %2").arg(key, choices.join(QStringLiteral(", "))));
    *out = value.toString();
    return std::nullopt;
}

std::optional<PluginHostApi::Reply>
readName(const QJsonValue& value, QString* out)
{
    if (!value.isString() || !FileOperationService::isValidName(value.toString().toStdString()))
        return fail(kInvalidParams, QStringLiteral("name must be a non-blank name without / or \\"));
    *out = value.toString();
    return std::nullopt;
}

// A file or folder outside the Rubbish bin.
std::optional<PluginHostApi::Reply> readLiveNode(const IMegaClient& client, const QJsonValue& value, NodeSnapshot* out)
{
    std::uint64_t handle = 0;
    if (std::optional<PluginHostApi::Reply> error = decodeHandle(client, value, &handle))
        return error;
    const Result<NodeSnapshot> node = client.getNodeSnapshot(handle);
    if (!node.success || node.value().inRubbish)
        return fail(PluginHostApi::kItemNotFound, QStringLiteral("No such item: %1").arg(value.toString()));
    *out = node.value();
    return std::nullopt;
}

std::optional<PluginHostApi::Reply>
readDestination(const IMegaClient& client, const QJsonValue& value, NodeSnapshot* out)
{
    if (std::optional<PluginHostApi::Reply> error = readNode(client, value, true, out))
        return error;
    if (out->inRubbish)
        return fail(PluginHostApi::kItemNotFound, QStringLiteral("No such item: %1").arg(value.toString()));
    return std::nullopt;
}

// Leaves *out alone when the param is absent.
std::optional<PluginHostApi::Reply> readOptionalName(const QJsonObject& params, QString* out)
{
    const QJsonValue value = params.value(QStringLiteral("name"));
    if (value.isUndefined() || value.isNull())
        return std::nullopt;
    return readName(value, out);
}

// What checkMove / checkUpload refusing a destination means to a plugin.
PluginHostApi::Reply refusedPlacement(const Result<void>& refusal)
{
    if (refusal.errorCode == MegaErrorCode::kENoEnt)
        return fail(PluginHostApi::kItemNotFound, QStringLiteral("The item or the folder no longer exists"));
    PluginHostApi::Reply reply = refusal.errorCode == MegaErrorCode::kECircular
                                     ? fail(kInvalidParams, QStringLiteral("A folder cannot go inside itself"))
                                     : megaFail(refusal.errorMessage);
    if (refusal.errorCode == MegaErrorCode::kECircular)
        reply.errorData = QJsonObject{{QStringLiteral("reason"), QStringLiteral("circular")}};
    else if (refusal.errorCode == MegaErrorCode::kEAccess)
        reply.errorData = QJsonObject{{QStringLiteral("reason"), QStringLiteral("access")}};
    return reply;
}

struct Placement
{
    bool clash = false;
    std::set<std::string> taken;
};

// Only an item of the same type clashes: MEGA lets a file and a folder share a name.
Placement placementIn(const IMegaClient& client, std::uint64_t folder, bool isFolder, const QString& name)
{
    Placement placement;
    if (const Result<std::vector<NodeSnapshot>> children = client.getChildSnapshots(folder); children.success)
    {
        for (const NodeSnapshot& child : children.value())
        {
            placement.taken.insert(child.name);
            placement.clash = placement.clash || (child.isFolder == isFolder && child.name == name.toStdString());
        }
    }
    return placement;
}

// Runs start unless versioning is off, when making a version would delete the old
// file for good instead of keeping it.
void whenVersioningKeepsTheOldFile(const std::shared_ptr<IMegaClient>& client,
                                   QObject* guiContext,
                                   const PluginHostApi::Done& finish,
                                   std::function<void()> start)
{
    client->getFileVersioningEnabled([guiContext, finish, start = std::move(start)](Result<bool> result) {
        invokeOnGuiThread(guiContext, [finish, start, result = std::move(result)] {
            // kENoEnt: the account never touched the setting, which means enabled.
            if (!result.success && result.errorCode != MegaErrorCode::kENoEnt)
                return finish(megaFail(result.errorMessage));
            if (result.success && !result.value())
                return finish(conflict(QStringLiteral("A file with that name already exists"),
                                       QStringLiteral("versioningDisabled"),
                                       QStringLiteral("Versioning is off for this account, so replacing would delete the "
                                                      "existing file permanently.")));
            start();
        });
    });
}

std::optional<PluginHostApi::Reply>
readBytes(const QJsonObject& params, const QString& key, bool required, std::optional<std::uint64_t>* out)
{
    const QJsonValue value = params.value(key);
    if (value.isUndefined() || value.isNull())
    {
        if (required)
            return fail(kInvalidParams, QStringLiteral("%1 is required").arg(key));
        out->reset();
        return std::nullopt;
    }
    const double number = value.toDouble(-1);
    // 2^53: beyond it a JSON number no longer holds every whole value.
    if (!value.isDouble() || number < 0 || number != std::floor(number) || number > 9007199254740992.0)
        return fail(kInvalidParams, QStringLiteral("%1 must be a whole number of bytes").arg(key));
    *out = static_cast<std::uint64_t>(number);
    return std::nullopt;
}

struct ByteRange
{
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
};

// offset/length as a range inside node, the length cut at the end of the file.
// nullopt in *out when neither is given and they are not required.
std::optional<PluginHostApi::Reply>
readByteRange(const QJsonObject& params, const NodeSnapshot& node, bool required, std::optional<ByteRange>* out)
{
    std::optional<std::uint64_t> offset;
    std::optional<std::uint64_t> length;
    if (std::optional<PluginHostApi::Reply> error = readBytes(params, QStringLiteral("offset"), required, &offset))
        return error;
    if (std::optional<PluginHostApi::Reply> error = readBytes(params, QStringLiteral("length"), required, &length))
        return error;
    if (!offset && !length)
    {
        out->reset();
        return std::nullopt;
    }
    const std::uint64_t start = offset.value_or(0);
    if (start >= node.sizeBytes)
        return fail(kInvalidParams,
                    QStringLiteral("offset %1 is at or past the end of the file (%2 bytes)")
                        .arg(start)
                        .arg(node.sizeBytes));
    const std::uint64_t available = node.sizeBytes - start;
    const std::uint64_t count = length ? std::min(*length, available) : available;
    if (count == 0)
        return fail(kInvalidParams, QStringLiteral("length must be more than 0"));
    *out = ByteRange{start, count};
    return std::nullopt;
}

// A relative folder path inside the Downloads folder, with '/' separators; empty for its top.
std::optional<PluginHostApi::Reply> readSubPath(const QJsonValue& value, QString* out)
{
    out->clear();
    if (value.isUndefined() || value.isNull())
        return std::nullopt;
    const auto bad = [&value] {
        return fail(kInvalidParams,
                    QStringLiteral("subPath \"%1\" must be a relative folder path, without .. or characters "
                                   "Windows does not allow")
                        .arg(value.toString()));
    };
    if (!value.isString())
        return bad();
    QString path = value.toString();
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (path.startsWith(QLatin1Char('/')))
        return bad();
    static const QRegularExpression forbidden(QStringLiteral("[<>:\"|?*\\x00-\\x1f]"));
    QStringList parts;
    for (const QString& part : path.split(QLatin1Char('/'), Qt::SkipEmptyParts))
    {
        // Windows drops a trailing dot or space, which would turn ".." back on.
        if (part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' ')) || part.contains(forbidden))
            return bad();
        parts.append(part);
    }
    *out = parts.join(QLatin1Char('/'));
    return std::nullopt;
}

void pump(const std::shared_ptr<TransferQueue>& queue)
{
    while (!queue->busy && !queue->pending.empty())
    {
        auto [job, done] = std::move(queue->pending.front());
        queue->pending.pop_front();
        queue->busy = true;
        job(queue, [queue, done = std::move(done)](const PluginHostApi::Reply& reply) {
            queue->busy = false;
            queue->abort = nullptr;
            done(queue->cancelled && reply.errorCode ? cancelledReply(reply.mutated) : reply);
            pump(queue);
        });
    }
}

void enqueueTransfer(PluginHostApi::RunState& run, TransferQueue::Job job, const PluginHostApi::Done& done)
{
    if (!run.transfers)
        run.transfers = std::make_shared<TransferQueue>();
    if (run.transfers->cancelled)
    {
        done(cancelledReply());
        return;
    }
    run.transfers->pending.emplace_back(std::move(job), done);
    pump(run.transfers);
}

void fetchWholeFile(const std::shared_ptr<IMegaClient>& client,
                    QObject* guiContext,
                    const NodeSnapshot& node,
                    const QString& dir,
                    const std::shared_ptr<TransferQueue>& queue,
                    const PluginHostApi::Done& finish)
{
    const QString path = QDir::toNativeSeparators(
        QDir(dir).filePath(QString::fromStdString(DownloadService::safeLocalFileName(node.name))));
    const std::uint64_t id = nextPluginTransferId();
    queue->abort = [client, id] { client->cancelDownload(id); };
    client->download(
        node.handle,
        path.toStdString(),
        id,
        [](std::uint64_t, std::uint64_t) {},
        [guiContext, finish, dir](Result<DownloadOutcome> result) {
            invokeOnGuiThread(guiContext, [finish, dir, result = std::move(result)] {
                if (result.success)
                {
                    finish(ok(QJsonObject{
                        {QStringLiteral("path"),
                         QDir::toNativeSeparators(QString::fromStdString(result.value().localPath))}}));
                    return;
                }
                QDir(dir).removeRecursively();
                finish(result.errorCode == MegaErrorCode::kEIncomplete ? cancelledReply()
                                                                         : megaFail(result.errorMessage));
            });
        });
}

// What a streamed range read writes into; filled on the SDK's thread, read on the
// GUI thread only after onDone.
struct RangeSink
{
    std::ofstream file;
    QByteArray buffer;
    bool toFile = false;
    bool writeFailed = false;
    std::atomic<bool> aborted{false};
};

void readRangeInto(const std::shared_ptr<IMegaClient>& client,
                   QObject* guiContext,
                   std::uint64_t handle,
                   ByteRange range,
                   const std::shared_ptr<RangeSink>& sink,
                   const std::shared_ptr<TransferQueue>& queue,
                   std::function<void(const Result<void>&)> onDone)
{
    queue->abort = [sink] { sink->aborted = true; };
    client->readFileRangeStreamed(
        handle,
        range.offset,
        range.length,
        [sink](const char* data, std::size_t size) {
            if (sink->aborted)
                return false;
            if (!sink->toFile)
            {
                sink->buffer.append(data, static_cast<qsizetype>(size));
                return true;
            }
            sink->file.write(data, static_cast<std::streamsize>(size));
            sink->writeFailed = !sink->file;
            return !sink->writeFailed;
        },
        [guiContext, onDone = std::move(onDone)](Result<void> result) {
            invokeOnGuiThread(guiContext, [onDone, result = std::move(result)] { onDone(result); });
        });
}

void fetchFileRange(const std::shared_ptr<IMegaClient>& client,
                    QObject* guiContext,
                    const NodeSnapshot& node,
                    ByteRange range,
                    const QString& dir,
                    const std::shared_ptr<TransferQueue>& queue,
                    const PluginHostApi::Done& finish)
{
    const QString path = QDir::toNativeSeparators(QDir(dir).filePath(
        QStringLiteral("%1.%2-%3.part").arg(QString::number(node.handle, 16)).arg(range.offset).arg(range.length)));
    auto sink = std::make_shared<RangeSink>();
    sink->toFile = true;
    sink->file.open(std::filesystem::path(path.toStdWString()), std::ios::binary);
    if (!sink->file)
    {
        finish(fail(kInternalError, QStringLiteral("Could not create %1").arg(path)));
        return;
    }
    readRangeInto(client, guiContext, node.handle, range, sink, queue, [finish, dir, path, sink](const Result<void>& result) {
        sink->file.close();
        if (result.success && sink->file)
        {
            finish(ok(QJsonObject{{QStringLiteral("path"), path}}));
            return;
        }
        QDir(dir).removeRecursively();
        if (sink->aborted)
            finish(cancelledReply());
        else if (sink->writeFailed || result.success)
            finish(fail(kInternalError, QStringLiteral("Could not write %1").arg(path)));
        else
            finish(megaFail(result.errorMessage));
    });
}
} // namespace

void PluginHostApi::cancelTransfers(RunState& run)
{
    const std::shared_ptr<TransferQueue> queue = run.transfers;
    if (!queue)
        return;
    queue->cancelled = true;
    const auto pending = std::exchange(queue->pending, {});
    for (const auto& entry : pending)
        entry.second(cancelledReply());
    if (queue->abort)
        queue->abort();
}

void PluginHostApi::abandonTransfers(RunState& run)
{
    const std::shared_ptr<TransferQueue> queue = run.transfers;
    if (!queue)
        return;
    queue->cancelled = true;
    queue->pending.clear();
    if (queue->abort)
        queue->abort();
}

void PluginHostApi::itemsFetchFile(const QJsonObject& params, RunState& run, const Done& done) const
{
    NodeSnapshot node;
    if (std::optional<Reply> error = readNode(*mClient, params.value(QStringLiteral("handle")), false, &node))
        return done(*error);
    std::optional<ByteRange> range;
    if (std::optional<Reply> error = readByteRange(params, node, false, &range))
        return done(*error);
    if (run.tempDir.isEmpty())
        return done(fail(kInternalError, QStringLiteral("This run has no folder for fetched files")));

    enqueueTransfer(
        run,
        [client = mClient, guiContext = mGuiContext, node, range, tempDir = run.tempDir](
            const std::shared_ptr<TransferQueue>& queue, const Done& finish) {
            const QString dir = QDir(tempDir).filePath(QStringLiteral("files/%1").arg(++queue->nextSlot));
            if (!QDir().mkpath(dir))
                return finish(fail(kInternalError, QStringLiteral("Could not create %1").arg(dir)));
            if (range)
                fetchFileRange(client, guiContext, node, *range, dir, queue, finish);
            else
                fetchWholeFile(client, guiContext, node, dir, queue, finish);
        },
        done);
}

void PluginHostApi::itemsReadRange(const QJsonObject& params, RunState& run, const Done& done) const
{
    NodeSnapshot node;
    if (std::optional<Reply> error = readNode(*mClient, params.value(QStringLiteral("handle")), false, &node))
        return done(*error);
    std::optional<ByteRange> range;
    if (std::optional<Reply> error = readByteRange(params, node, true, &range))
        return done(*error);
    if (range->length > kMaxReadRangeBytes)
        return done(fail(kInvalidParams,
                         QStringLiteral("items.readRange reads at most %1 bytes at a time; use items.fetchFile "
                                        "with offset and length for more")
                             .arg(kMaxReadRangeBytes)));

    enqueueTransfer(
        run,
        [client = mClient, guiContext = mGuiContext, handle = node.handle, byteRange = *range](
            const std::shared_ptr<TransferQueue>& queue, const Done& finish) {
            auto sink = std::make_shared<RangeSink>();
            readRangeInto(client, guiContext, handle, byteRange, sink, queue, [finish, sink](const Result<void>& result) {
                if (sink->aborted)
                    return finish(cancelledReply());
                if (!result.success)
                    return finish(megaFail(result.errorMessage));
                finish(ok(QJsonObject{{QStringLiteral("data"), QString::fromLatin1(sink->buffer.toBase64())},
                                      {QStringLiteral("length"), static_cast<double>(sink->buffer.size())}}));
            });
        },
        done);
}

void PluginHostApi::itemsUpload(const QJsonObject& params, RunState& run, const Done& done) const
{
    NodeSnapshot parent;
    if (std::optional<Reply> error = readNode(*mClient, params.value(QStringLiteral("parent")), true, &parent))
        return done(*error);
    const QJsonValue localPath = params.value(QStringLiteral("localPath"));
    const QFileInfo local(localPath.toString());
    if (!localPath.isString() || !local.isAbsolute() || !local.isFile())
        return done(fail(kInvalidParams, QStringLiteral("localPath must be the absolute path of an existing file")));
    QString name = local.fileName();
    const QJsonValue nameValue = params.value(QStringLiteral("name"));
    if (!nameValue.isUndefined() && !nameValue.isNull())
    {
        if (std::optional<Reply> error = readName(nameValue, &name))
            return done(*error);
    }
    QString onConflict;
    if (std::optional<Reply> error = readChoice(params,
                                                QStringLiteral("onConflict"),
                                                {QStringLiteral("rename"), QStringLiteral("fail"), QStringLiteral("version")},
                                                &onConflict))
        return done(*error);
    const Result<void> allowed = mClient->checkUpload(parent.handle, false);
    if (!allowed.success)
        return done(allowed.errorCode == MegaErrorCode::kENoEnt
                        ? fail(kItemNotFound, QStringLiteral("The folder no longer exists"))
                        : megaFail(allowed.errorMessage));

    enqueueTransfer(
        run,
        [client = mClient,
         guiContext = mGuiContext,
         path = QDir::toNativeSeparators(local.absoluteFilePath()).toStdString(),
         parentHandle = parent.handle,
         requestedName = name.toStdString(),
         onConflict](const std::shared_ptr<TransferQueue>& queue, const Done& finish) {
            if (queue->cancelled)
                return finish(cancelledReply());
            // Resolved here, not when the request arrived: an upload queued ahead of
            // this one may have taken the name since.
            std::string nodeName = requestedName;
            std::set<std::string> taken;
            bool clash = false;
            if (const Result<std::vector<NodeSnapshot>> children = client->getChildSnapshots(parentHandle);
                children.success)
            {
                for (const NodeSnapshot& child : children.value())
                {
                    taken.insert(child.name);
                    clash = clash || (!child.isFolder && child.name == requestedName);
                }
            }
            bool checkVersioning = false;
            if (clash)
            {
                if (onConflict == QLatin1String("fail"))
                    return finish(conflict(
                        QStringLiteral("A file named %1 already exists there").arg(QString::fromStdString(requestedName)),
                        QStringLiteral("exists")));
                if (onConflict == QLatin1String("rename"))
                    nodeName = FileOperationService::uniqueMoveName(requestedName, false, taken);
                else
                    checkVersioning = true;
            }

            const auto start = [client, guiContext, path, parentHandle, nodeName, queue, finish] {
                if (queue->cancelled)
                    return finish(cancelledReply());
                const std::uint64_t id = nextPluginTransferId();
                queue->abort = [client, id] { client->cancelUpload(id); };
                client->upload(
                    path,
                    parentHandle,
                    false,
                    nodeName,
                    id,
                    [](std::uint64_t, std::uint64_t) {},
                    [client, guiContext, finish](Result<UploadOutcome> result) {
                        invokeOnGuiThread(guiContext, [client, finish, result = std::move(result)] {
                            if (!result.success)
                                return finish(result.errorCode == MegaErrorCode::kEIncomplete
                                                  ? cancelledReply()
                                                  : megaFail(result.errorMessage));
                            Reply reply = ok(QJsonObject{
                                {QStringLiteral("item"), createdItem(*client, result.value().nodeHandle)}});
                            reply.mutated = true;
                            finish(reply);
                        });
                    });
            };
            if (!checkVersioning)
                return start();
            whenVersioningKeepsTheOldFile(client, guiContext, finish, start);
        },
        done);
}

void PluginHostApi::itemsCreateFolder(const QJsonObject& params, const Done& done) const
{
    NodeSnapshot parent;
    if (std::optional<Reply> error = readNode(*mClient, params.value(QStringLiteral("parent")), true, &parent))
        return done(*error);
    QString name;
    if (std::optional<Reply> error = readName(params.value(QStringLiteral("name")), &name))
        return done(*error);
    QString onConflict;
    if (std::optional<Reply> error = readChoice(params,
                                                QStringLiteral("onConflict"),
                                                {QStringLiteral("existing"), QStringLiteral("fail"), QStringLiteral("rename")},
                                                &onConflict))
        return done(*error);

    std::set<std::string> taken;
    std::optional<NodeSnapshot> existing;
    if (const Result<std::vector<NodeSnapshot>> children = mClient->getChildSnapshots(parent.handle);
        children.success)
    {
        for (const NodeSnapshot& child : children.value())
        {
            taken.insert(child.name);
            if (child.isFolder && !existing && child.name == name.toStdString())
                existing = child;
        }
    }
    if (existing)
    {
        if (onConflict == QLatin1String("existing"))
            return done(ok(QJsonObject{{QStringLiteral("item"), toItem(*mClient, *existing)},
                                       {QStringLiteral("created"), false}}));
        if (onConflict == QLatin1String("fail"))
            return done(conflict(QStringLiteral("A folder named %1 already exists there").arg(name), QStringLiteral("exists")));
        name = QString::fromStdString(FileOperationService::uniqueMoveName(name.toStdString(), true, taken));
    }

    mClient->createFolder(
        parent.handle,
        false,
        name.toStdString(),
        [client = mClient, guiContext = mGuiContext, done, name](Result<std::uint64_t> result) {
            invokeOnGuiThread(guiContext, [client, done, name, result = std::move(result)] {
                if (!result.success)
                    return done(result.errorCode == MegaErrorCode::kEExist
                                    ? conflict(QStringLiteral("A folder named %1 already exists there").arg(name),
                                               QStringLiteral("exists"))
                                    : megaFail(result.errorMessage));
                Reply reply = ok(QJsonObject{{QStringLiteral("item"), createdItem(*client, result.value())},
                                             {QStringLiteral("created"), true}});
                reply.mutated = true;
                done(reply);
            });
        });
}

void PluginHostApi::itemsMove(const QJsonObject& params, const Done& done) const
{
    NodeSnapshot node;
    if (std::optional<Reply> error = readLiveNode(*mClient, params.value(QStringLiteral("handle")), &node))
        return done(*error);
    if (!node.hasParent)
        return done(fail(kInvalidParams, QStringLiteral("A root cannot be moved")));
    NodeSnapshot target;
    if (std::optional<Reply> error = readDestination(*mClient, params.value(QStringLiteral("to")), &target))
        return done(*error);
    QString name = QString::fromStdString(node.name);
    if (std::optional<Reply> error = readOptionalName(params, &name))
        return done(*error);
    QString onConflict;
    if (std::optional<Reply> error = readChoice(params,
                                                QStringLiteral("onConflict"),
                                                {QStringLiteral("rename"), QStringLiteral("fail"), QStringLiteral("version")},
                                                &onConflict))
        return done(*error);
    if (onConflict == QLatin1String("version"))
        return done(fail(kInvalidParams, QStringLiteral("onConflict \"version\" is for copying a file only")));

    // Already there is a success, so a sorting plugin can be run again.
    if (node.parentHandle == target.handle)
    {
        if (name.toStdString() != node.name)
            return done(fail(kInvalidParams, QStringLiteral("The item is already in that folder; rename it with items.update")));
        return done(ok(QJsonObject{{QStringLiteral("item"), toItem(*mClient, node)}, {QStringLiteral("moved"), false}}));
    }
    if (const Result<void> allowed = mClient->checkMove(node.handle, target.handle, false); !allowed.success)
        return done(refusedPlacement(allowed));

    const Placement placement = placementIn(*mClient, target.handle, node.isFolder, name);
    if (placement.clash)
    {
        if (onConflict == QLatin1String("fail"))
            return done(conflict(QStringLiteral("%1 already exists there").arg(name), QStringLiteral("exists")));
        name = QString::fromStdString(FileOperationService::uniqueMoveName(name.toStdString(), node.isFolder, placement.taken));
    }

    const std::string newName = name.toStdString() == node.name ? std::string() : name.toStdString();
    mClient->moveNode(
        node.handle,
        target.handle,
        false,
        newName,
        [client = mClient, guiContext = mGuiContext, done, handle = node.handle](Result<void> result) {
            invokeOnGuiThread(guiContext, [client, done, handle, result = std::move(result)] {
                if (!result.success)
                    return done(result.errorCode == MegaErrorCode::kENoEnt
                                    ? fail(kItemNotFound, QStringLiteral("The item or the folder no longer exists"))
                                    : megaFail(result.errorMessage));
                Reply reply = ok(QJsonObject{{QStringLiteral("item"), createdItem(*client, handle)},
                                             {QStringLiteral("moved"), true}});
                reply.mutated = true;
                done(reply);
            });
        });
}

void PluginHostApi::itemsCopy(const QJsonObject& params, const Done& done) const
{
    NodeSnapshot node;
    if (std::optional<Reply> error = readLiveNode(*mClient, params.value(QStringLiteral("handle")), &node))
        return done(*error);
    if (!node.hasParent)
        return done(fail(kInvalidParams, QStringLiteral("A root cannot be copied")));
    NodeSnapshot target;
    if (std::optional<Reply> error = readDestination(*mClient, params.value(QStringLiteral("to")), &target))
        return done(*error);
    QString name = QString::fromStdString(node.name);
    if (std::optional<Reply> error = readOptionalName(params, &name))
        return done(*error);
    QString onConflict;
    if (std::optional<Reply> error = readChoice(params,
                                                QStringLiteral("onConflict"),
                                                {QStringLiteral("rename"), QStringLiteral("fail"), QStringLiteral("version")},
                                                &onConflict))
        return done(*error);
    if (onConflict == QLatin1String("version") && node.isFolder)
        return done(fail(kInvalidParams, QStringLiteral("onConflict \"version\" is for copying a file only")));
    if (const Result<void> allowed = mClient->checkUpload(target.handle, false); !allowed.success)
        return done(refusedPlacement(allowed));

    // A copy into its own folder clashes with itself, so it is renamed like any other.
    const Placement placement = placementIn(*mClient, target.handle, node.isFolder, name);
    bool makeVersion = false;
    if (placement.clash)
    {
        if (onConflict == QLatin1String("fail"))
            return done(conflict(QStringLiteral("%1 already exists there").arg(name), QStringLiteral("exists")));
        if (onConflict == QLatin1String("rename"))
            name = QString::fromStdString(FileOperationService::uniqueMoveName(name.toStdString(), node.isFolder, placement.taken));
        else
            makeVersion = true;
    }

    // An empty name keeps the source's (IMegaClient::copyNode).
    const std::string newName = name.toStdString() == node.name ? std::string() : name.toStdString();
    const auto start = [client = mClient, guiContext = mGuiContext, done, handle = node.handle, to = target.handle, newName] {
        client->copyNode(handle, to, false, newName, [client, guiContext, done](Result<std::uint64_t> result) {
            invokeOnGuiThread(guiContext, [client, done, result = std::move(result)] {
                if (!result.success)
                    return done(result.errorCode == MegaErrorCode::kENoEnt
                                    ? fail(kItemNotFound, QStringLiteral("The item or the folder no longer exists"))
                                    : megaFail(result.errorMessage));
                Reply reply = ok(QJsonObject{{QStringLiteral("item"), createdItem(*client, result.value())}});
                reply.mutated = true;
                done(reply);
            });
        });
    };
    if (!makeVersion)
        return start();
    whenVersioningKeepsTheOldFile(mClient, mGuiContext, done, start);
}

void PluginHostApi::itemsMoveToRubbish(const QJsonObject& params, const Done& done) const
{
    NodeSnapshot node;
    if (std::optional<Reply> error = readLiveNode(*mClient, params.value(QStringLiteral("handle")), &node))
        return done(*error);
    if (!node.hasParent)
        return done(fail(kInvalidParams, QStringLiteral("A root cannot be moved to the Rubbish bin")));
    mClient->moveToRubbish(node.handle, [guiContext = mGuiContext, done](Result<void> result) {
        invokeOnGuiThread(guiContext, [done, result = std::move(result)] {
            if (!result.success)
                return done(result.errorCode == MegaErrorCode::kENoEnt
                                ? fail(kItemNotFound, QStringLiteral("The item no longer exists"))
                                : megaFail(result.errorMessage));
            Reply reply = ok(QJsonObject{});
            reply.mutated = true;
            done(reply);
        });
    });
}

PluginHostApi::Reply PluginHostApi::transfersDownload(const QJsonObject& params) const
{
    if (!mDownloads.enqueue || mDownloads.root.isEmpty())
        return fail(kInternalError, QStringLiteral("Downloads are not available"));
    const QJsonValue itemsValue = params.value(QStringLiteral("items"));
    if (!itemsValue.isArray() || itemsValue.toArray().isEmpty())
        return fail(kInvalidParams, QStringLiteral("items must be a non-empty array of {handle, subPath?}"));
    QString onConflict;
    if (std::optional<Reply> error = readChoice(params,
                                                QStringLiteral("onConflict"),
                                                {QStringLiteral("rename"), QStringLiteral("skip"), QStringLiteral("overwrite")},
                                                &onConflict))
        return *error;

    struct Planned
    {
        NodeSnapshot node;
        QString dir;
    };
    std::vector<Planned> planned;
    const QDir root(mDownloads.root);
    for (const QJsonValue& value : itemsValue.toArray())
    {
        if (!value.isObject())
            return fail(kInvalidParams, QStringLiteral("items must be a non-empty array of {handle, subPath?}"));
        const QJsonObject entry = value.toObject();
        Planned plan;
        if (std::optional<Reply> error = readNode(*mClient, entry.value(QStringLiteral("handle")), false, &plan.node))
            return *error;
        QString subPath;
        if (std::optional<Reply> error = readSubPath(entry.value(QStringLiteral("subPath")), &subPath))
            return *error;
        plan.dir = subPath.isEmpty() ? root.path() : root.filePath(subPath);
        planned.push_back(std::move(plan));
    }
    for (const Planned& plan : planned)
    {
        if (!QDir().mkpath(plan.dir))
            return fail(kInternalError, QStringLiteral("Could not create %1").arg(QDir::toNativeSeparators(plan.dir)));
    }

    int queued = 0;
    int skipped = 0;
    for (const Planned& plan : planned)
    {
        const QString name = QString::fromStdString(plan.node.name);
        const QString path = QDir::toNativeSeparators(
            QDir(plan.dir).filePath(QString::fromStdString(DownloadService::safeLocalFileName(plan.node.name))));
        if (mDownloads.isQueued && mDownloads.isQueued(plan.node.handle))
        {
            ++skipped;
            continue;
        }
        // "rename" needs nothing here: the download itself suffixes " (1)".
        if (QFileInfo::exists(path))
        {
            if (onConflict == QLatin1String("skip"))
            {
                ++skipped;
                continue;
            }
            if (onConflict == QLatin1String("overwrite") && !QFile::moveToTrash(path) && !QFile::remove(path))
                return fail(kInternalError,
                            QStringLiteral("Could not replace %1 (%2 file(s) were queued before it)").arg(path, QString::number(queued)));
        }
        if (mDownloads.enqueue(plan.node.handle, name, plan.node.sizeBytes, path))
            ++queued;
        else
            ++skipped;
    }
    return ok(QJsonObject{{QStringLiteral("queued"), queued}, {QStringLiteral("skipped"), skipped}});
}

PluginHostApi::Reply PluginHostApi::uiReveal(const QJsonObject& params) const
{
    std::uint64_t handle = 0;
    if (std::optional<Reply> error = readHandle(*mClient, params, &handle))
        return *error;
    const Result<NodeSnapshot> node = mClient->getNodeSnapshot(handle);
    if (!node.success)
        return fail(kItemNotFound, QStringLiteral("No such item"));
    if (!node.value().hasParent)
        return fail(kInvalidParams, QStringLiteral("A root is not in any folder"));
    if (mReveal)
        mReveal(handle, QString::fromStdString(node.value().name));
    return ok(QJsonObject{});
}

namespace
{
// Names rather than SearchFilter's ints, so reordering an enum cannot break a plugin.
template<typename E>
std::optional<PluginHostApi::Reply>
readSearchFacet(const QJsonObject& params, const QString& key, const QStringList& names, E* value)
{
    const QJsonValue raw = params.value(key);
    if (raw.isUndefined())
        return std::nullopt;
    const qsizetype index = raw.isString() ? names.indexOf(raw.toString()) : -1;
    if (index < 0)
        return fail(
            kInvalidParams,
            QStringLiteral("\"%1\" must be one of: %2").arg(key, names.join(QStringLiteral(", "))));
    *value = static_cast<E>(index);
    return std::nullopt;
}

std::optional<PluginHostApi::Reply>
readSearchFlag(const QJsonObject& params, const QString& key, bool* value)
{
    const QJsonValue raw = params.value(key);
    if (raw.isUndefined())
        return std::nullopt;
    if (!raw.isBool())
        return fail(kInvalidParams, QStringLiteral("\"%1\" must be true or false").arg(key));
    *value = raw.toBool();
    return std::nullopt;
}
} // namespace

PluginHostApi::Reply PluginHostApi::uiSearch(const QJsonObject& params) const
{
    const QJsonValue rawQuery = params.value(QStringLiteral("query"));
    if (!rawQuery.isUndefined() && !rawQuery.isString())
        return fail(kInvalidParams, QStringLiteral("\"query\" must be a string"));
    const QString query = rawQuery.toString();
    for (const QChar c : query)
    {
        if (c.category() == QChar::Other_Control)
            return fail(kInvalidParams, QStringLiteral("\"query\" must be a single line"));
    }

    // Same order as the enums in core/SearchFilter.h.
    static const QStringList nodeTypes{
        QStringLiteral("any"), QStringLiteral("files"), QStringLiteral("folders")};
    static const QStringList categories{QStringLiteral("any"),
                                        QStringLiteral("photo"),
                                        QStringLiteral("audio"),
                                        QStringLiteral("video"),
                                        QStringLiteral("document"),
                                        QStringLiteral("pdf"),
                                        QStringLiteral("presentation"),
                                        QStringLiteral("spreadsheet"),
                                        QStringLiteral("archive"),
                                        QStringLiteral("program"),
                                        QStringLiteral("other")};
    static const QStringList timeWindows{QStringLiteral("any"),
                                         QStringLiteral("pastDay"),
                                         QStringLiteral("pastWeek"),
                                         QStringLiteral("pastMonth"),
                                         QStringLiteral("pastYear")};

    SearchFilter filter;
    if (auto error = readSearchFacet(params, QStringLiteral("type"), nodeTypes, &filter.nodeType))
        return *error;
    if (auto error =
            readSearchFacet(params, QStringLiteral("category"), categories, &filter.category))
        return *error;
    if (auto error = readSearchFacet(
            params, QStringLiteral("createdWithin"), timeWindows, &filter.createdWithin))
        return *error;
    if (auto error =
            readSearchFlag(params, QStringLiteral("favouritesOnly"), &filter.favouritesOnly))
        return *error;
    if (auto error =
            readSearchFlag(params, QStringLiteral("thisFolderOnly"), &filter.thisFolderOnly))
        return *error;

    if (mSearch)
        mSearch(query, filter);
    return ok(QJsonObject{});
}
