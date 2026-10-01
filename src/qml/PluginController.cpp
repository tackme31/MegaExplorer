#include "PluginController.h"

#include "app/Logging.h"
#include "PluginRun.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <utility>
#include <windows.h>

namespace
{
const QString kActionPrefix = QStringLiteral("plugin:");
// A command that finishes sooner never shows the dialog, so quick ones don't flash it.
constexpr int kProgressShowDelayMs = 300;
constexpr int kProgressUpdateIntervalMs = 100;

bool isProcessRunning(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return GetLastError() == ERROR_ACCESS_DENIED;
    DWORD exitCode = 0;
    const bool running = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
    CloseHandle(process);
    return running;
}

// Per process rather than wiping tempRoot: two copies of the app may share a profile.
void removeStaleTempDirs(const QString& tempRoot)
{
    if (tempRoot.isEmpty())
        return;
    const QDir root(tempRoot);
    for (const QString& name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
        bool isPid = false;
        const DWORD pid = name.toULong(&isPid);
        if (isPid && !isProcessRunning(pid))
            QDir(root.filePath(name)).removeRecursively();
    }
}
} // namespace

PluginController::PluginController(std::shared_ptr<IMegaClient> client,
                                   QString pluginsDir,
                                   const QString& tempRoot,
                                   QObject* parent)
    : QObject(parent), mHostApi(std::move(client), this), mProgressUpdateTimer(new QTimer(this)),
      mPluginsDir(std::move(pluginsDir)),
      mTempDir(QDir(tempRoot).filePath(QString::number(QCoreApplication::applicationPid())))
{
    removeStaleTempDirs(tempRoot);
    mProgressUpdateTimer->setSingleShot(true);
    mProgressUpdateTimer->setInterval(kProgressUpdateIntervalMs);
    connect(mProgressUpdateTimer, &QTimer::timeout, this, &PluginController::progressRunsChanged);
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
    const PluginCommand* command = findCommand(actionId);
    return command ? command->title : QString();
}

bool PluginController::canRun(const QString& actionId) const
{
    QString pluginId;
    QString commandId;
    return splitActionId(actionId, &pluginId, &commandId) && !mRuns.contains(pluginId);
}

bool PluginController::accepts(const QString& actionId, const QVariantList& entries) const
{
    const PluginCommand* command = findCommand(actionId);
    if (!command)
        return false;
    std::vector<PluginSelectionItem> selection;
    selection.reserve(static_cast<std::size_t>(entries.size()));
    for (const QVariant& value : entries)
    {
        const QVariantMap entry = value.toMap();
        selection.push_back({entry.value(QStringLiteral("name")).toString(),
                             entry.value(QStringLiteral("isFolder")).toBool()});
    }
    return pluginCommandAccepts(*command, selection);
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
        if (std::optional<QJsonObject> item = mHostApi.item(handle))
            items.append(*item);
    }
    const QJsonObject context{{QStringLiteral("site"), QStringLiteral("selection")},
                              {QStringLiteral("items"), items}};

    const QString tempDir = QDir(mTempDir).filePath(QString::number(++mRunCount));
    auto* run = new PluginRun(*plugin, commandId, context, tempDir, &mHostApi, this);
    const QString pluginName = plugin->name;
    connect(run,
            &PluginRun::finished,
            this,
            [this, pluginId, pluginName](const QString& outcome, const QString& message, bool changed) {
                mRuns.remove(pluginId);
                removeConfirm(pluginId);
                const auto progress =
                    std::find_if(mProgress.begin(), mProgress.end(), [&](const ProgressState& state) {
                        return state.pluginId == pluginId;
                    });
                if (progress != mProgress.end())
                {
                    const bool wasShown = progress->shown;
                    mProgress.erase(progress);
                    if (wasShown)
                        emit progressRunsChanged();
                }
                ++mRunningRevision;
                emit runningChanged();
                emit commandFinished(pluginName, outcome, message, changed);
            });
    connect(run,
            &PluginRun::confirmRequested,
            this,
            [this, pluginId, pluginName](const QString& title,
                                         const QString& message,
                                         const QString& okLabel,
                                         bool danger) {
                mConfirms.push_back({pluginId, pluginName, title, message, okLabel, danger});
                emit confirmRequestsChanged();
            });
    const auto command =
        std::find_if(plugin->commands.begin(), plugin->commands.end(), [&](const PluginCommand& c) {
            return c.id == commandId;
        });
    if (command != plugin->commands.end() && command->progress)
    {
        mProgress.push_back(
            {pluginId, pluginName, command->title, QDateTime::currentMSecsSinceEpoch()});
        connect(run, &PluginRun::executionStarted, this, [this, pluginId] {
            if (ProgressState* state = findProgress(pluginId))
            {
                state->preparing = false;
                scheduleProgressUpdate();
            }
        });
        connect(run,
                &PluginRun::progressReported,
                this,
                [this, pluginId](qint64 current, qint64 total, const QString& message) {
                    if (ProgressState* state = findProgress(pluginId))
                    {
                        state->current = current;
                        state->total = total;
                        state->message = message;
                        scheduleProgressUpdate();
                    }
                });
        connect(run, &PluginRun::cancelIgnored, this, [this, pluginId] {
            if (ProgressState* state = findProgress(pluginId))
            {
                state->forceStoppable = true;
                emit progressRunsChanged();
            }
        });
        QTimer::singleShot(kProgressShowDelayMs, this, [this, pluginId] {
            if (ProgressState* state = findProgress(pluginId))
            {
                state->shown = true;
                emit progressRunsChanged();
            }
        });
    }
    mRuns.insert(pluginId, run);
    ++mRunningRevision;
    emit runningChanged();
    run->start();
}

QVariantList PluginController::progressRuns() const
{
    QVariantList runs;
    for (const ProgressState& state : mProgress)
    {
        if (!state.shown)
            continue;
        runs.append(QVariantMap{{QStringLiteral("pluginId"), state.pluginId},
                                {QStringLiteral("pluginName"), state.pluginName},
                                {QStringLiteral("commandTitle"), state.commandTitle},
                                {QStringLiteral("startedAt"), state.startedAt},
                                {QStringLiteral("preparing"), state.preparing},
                                {QStringLiteral("current"), state.current},
                                {QStringLiteral("total"), state.total},
                                {QStringLiteral("message"), state.message},
                                {QStringLiteral("cancelling"), state.cancelling},
                                {QStringLiteral("forceStoppable"), state.forceStoppable}});
    }
    return runs;
}

void PluginController::cancel(const QString& pluginId)
{
    PluginRun* run = mRuns.value(pluginId);
    if (!run)
        return;
    if (ProgressState* state = findProgress(pluginId); state && !state->cancelling)
    {
        state->cancelling = true;
        emit progressRunsChanged();
    }
    run->cancel();
}

void PluginController::forceStop(const QString& pluginId)
{
    if (PluginRun* run = mRuns.value(pluginId))
        run->forceStop();
}

QVariantList PluginController::confirmRequests() const
{
    QVariantList requests;
    for (const ConfirmState& state : mConfirms)
    {
        requests.append(QVariantMap{{QStringLiteral("pluginId"), state.pluginId},
                                    {QStringLiteral("pluginName"), state.pluginName},
                                    {QStringLiteral("title"), state.title},
                                    {QStringLiteral("message"), state.message},
                                    {QStringLiteral("okLabel"), state.okLabel},
                                    {QStringLiteral("danger"), state.danger}});
    }
    return requests;
}

void PluginController::answerConfirm(const QString& pluginId, bool ok)
{
    removeConfirm(pluginId);
    if (PluginRun* run = mRuns.value(pluginId))
        run->answerConfirm(ok);
}

void PluginController::removeConfirm(const QString& pluginId)
{
    const auto confirm = std::find_if(mConfirms.begin(), mConfirms.end(), [&](const ConfirmState& state) {
        return state.pluginId == pluginId;
    });
    if (confirm == mConfirms.end())
        return;
    mConfirms.erase(confirm);
    emit confirmRequestsChanged();
}

PluginController::ProgressState* PluginController::findProgress(const QString& pluginId)
{
    for (ProgressState& state : mProgress)
    {
        if (state.pluginId == pluginId)
            return &state;
    }
    return nullptr;
}

void PluginController::scheduleProgressUpdate()
{
    if (!mProgressUpdateTimer->isActive())
        mProgressUpdateTimer->start();
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

const PluginCommand* PluginController::findCommand(const QString& actionId) const
{
    QString pluginId;
    QString commandId;
    if (!splitActionId(actionId, &pluginId, &commandId))
        return nullptr;
    const PluginManifest* plugin = findPlugin(pluginId);
    if (!plugin)
        return nullptr;
    for (const PluginCommand& command : plugin->commands)
    {
        if (command.id == commandId)
            return &command;
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
