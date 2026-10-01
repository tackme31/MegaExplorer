#include "PluginController.h"

#include "app/Logging.h"
#include "PluginRun.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>

#include <algorithm>
#include <utility>

namespace
{
const QString kActionPrefix = QStringLiteral("plugin:");
} // namespace

PluginController::PluginController(std::shared_ptr<IMegaClient> client,
                                   QString pluginsDir,
                                   QObject* parent)
    : QObject(parent), mHostApi(std::move(client)), mPluginsDir(std::move(pluginsDir))
{
    reload();
}

void PluginController::reload()
{
    mPlugins.clear();
    const QDir root(mPluginsDir);
    for (const QString& sub : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
    {
        const QString dir = root.absoluteFilePath(sub);
        QFile file(QDir(dir).filePath(QStringLiteral("plugin.json")));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QString error;
        std::optional<PluginManifest> manifest = parsePluginManifest(file.readAll(), dir, &error);
        if (!manifest)
        {
            qCWarning(lcPlugin) << "skipping" << dir << "-" << error;
            continue;
        }
        if (findPlugin(manifest->id))
        {
            qCWarning(lcPlugin) << "skipping" << dir << "- duplicate id" << manifest->id;
            continue;
        }
        qCInfo(lcPlugin) << "loaded" << manifest->id << manifest->version << "from" << dir;
        mPlugins.push_back(std::move(*manifest));
    }
    std::stable_sort(
        mPlugins.begin(), mPlugins.end(), [](const PluginManifest& a, const PluginManifest& b) {
            return a.name.localeAwareCompare(b.name) < 0;
        });
}

QStringList PluginController::menuActionIds() const
{
    QStringList ids;
    for (const PluginManifest& plugin : mPlugins)
    {
        for (const PluginCommand& command : plugin.commands)
            ids << kActionPrefix + plugin.id + QLatin1Char('/') + command.id;
    }
    return ids;
}

bool PluginController::isMenuActionId(const QString& actionId) const
{
    QString pluginId;
    QString commandId;
    return splitActionId(actionId, &pluginId, &commandId) && findPlugin(pluginId) != nullptr;
}

QString PluginController::groupOf(const QString& actionId) const
{
    QString pluginId;
    QString commandId;
    if (!splitActionId(actionId, &pluginId, &commandId))
        return {};
    return kActionPrefix + pluginId;
}

QString PluginController::groupLabel(const QString& group) const
{
    if (!group.startsWith(kActionPrefix))
        return {};
    const PluginManifest* plugin = findPlugin(group.mid(kActionPrefix.size()));
    return plugin ? plugin->name : QString();
}

QString PluginController::commandTitle(const QString& actionId) const
{
    QString pluginId;
    QString commandId;
    if (!splitActionId(actionId, &pluginId, &commandId))
        return {};
    if (const PluginManifest* plugin = findPlugin(pluginId))
    {
        for (const PluginCommand& command : plugin->commands)
        {
            if (command.id == commandId)
                return command.title;
        }
    }
    return {};
}

bool PluginController::canRun(const QString& actionId) const
{
    QString pluginId;
    QString commandId;
    return splitActionId(actionId, &pluginId, &commandId) && !mRuns.contains(pluginId);
}

void PluginController::execute(const QString& actionId, const QVariantList& entries)
{
    QString pluginId;
    QString commandId;
    if (!splitActionId(actionId, &pluginId, &commandId) || mRuns.contains(pluginId))
        return;
    const PluginManifest* plugin = findPlugin(pluginId);
    if (!plugin)
        return;

    // Re-read rather than taken from the row: the row has no parent, and a node
    // deleted since the menu opened is dropped instead of handed over.
    QJsonArray items;
    for (const QVariant& value : entries)
    {
        const quint64 handle = value.toMap().value(QStringLiteral("handle")).toULongLong();
        if (std::optional<QJsonObject> ref = mHostApi.itemRef(handle))
            items.append(*ref);
    }
    const QJsonObject context{{QStringLiteral("site"), QStringLiteral("selection")},
                              {QStringLiteral("items"), items}};

    auto* run = new PluginRun(*plugin, commandId, context, &mHostApi, this);
    const QString pluginName = plugin->name;
    connect(run,
            &PluginRun::finished,
            this,
            [this, pluginId, pluginName](const QString& outcome, const QString& message) {
                mRuns.remove(pluginId);
                ++mRunningRevision;
                emit runningChanged();
                emit commandFinished(pluginName, outcome, message);
            });
    mRuns.insert(pluginId, run);
    ++mRunningRevision;
    emit runningChanged();
    run->start();
}

const PluginManifest* PluginController::findPlugin(const QString& pluginId) const
{
    for (const PluginManifest& plugin : mPlugins)
    {
        if (plugin.id == pluginId)
            return &plugin;
    }
    return nullptr;
}

bool PluginController::splitActionId(const QString& actionId, QString* pluginId, QString* commandId)
{
    if (!actionId.startsWith(kActionPrefix))
        return false;
    const qsizetype slash = actionId.indexOf(QLatin1Char('/'), kActionPrefix.size());
    if (slash < 0)
        return false;
    *pluginId = actionId.mid(kActionPrefix.size(), slash - kActionPrefix.size());
    *commandId = actionId.mid(slash + 1);
    return !pluginId->isEmpty() && !commandId->isEmpty();
}
