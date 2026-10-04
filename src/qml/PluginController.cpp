#include "PluginController.h"

#include "app/Logging.h"
#include "PluginRun.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <utility>
#include <windows.h>

namespace
{
const QString kActionPrefix = QStringLiteral("plugin:");
// A command that finishes sooner never shows the dialog, so quick ones don't flash it.
constexpr int kProgressShowDelayMs = 300;
constexpr int kUnrequestedProgressShowDelayMs = 3000;
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
                                   PluginHostApi::UserDownloads downloads,
                                   QObject* parent)
    : QObject(parent),
      mHostApi(std::move(client),
               this,
               std::move(downloads),
               [this](std::uint64_t handle, const QString& name) {
                   emit revealRequested(static_cast<quint64>(handle), name);
               },
               [this](const QString& query, const SearchFilter& filter) {
                   emit searchRequested(query,
                                        static_cast<int>(filter.nodeType),
                                        static_cast<int>(filter.category),
                                        static_cast<int>(filter.createdWithin),
                                        filter.favouritesOnly,
                                        filter.thisFolderOnly);
               }),
      mProgressUpdateTimer(new QTimer(this)),
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
        if (manifest->apiVersion != kPluginApiVersion)
            qCWarning(lcPlugin) << manifest->id << "is written for plugin API" << manifest->apiVersion
                                << "but this app speaks" << kPluginApiVersion << "- its commands are disabled";
        mPlugins.push_back(std::move(*manifest));
    }
    std::stable_sort(
        mPlugins.begin(), mPlugins.end(), [](const PluginManifest& a, const PluginManifest& b) {
            return a.name.localeAwareCompare(b.name) < 0;
        });
}

QVariantList PluginController::installedPlugins() const
{
    QVariantList plugins;
    for (const PluginManifest& plugin : mPlugins)
        plugins.append(QVariantMap{{QStringLiteral("id"), plugin.id},
                                   {QStringLiteral("name"), plugin.name},
                                   {QStringLiteral("version"), plugin.version},
                                   {QStringLiteral("description"), plugin.description},
                                   {QStringLiteral("repositoryUrl"), plugin.repositoryUrl},
                                   {QStringLiteral("permissions"), plugin.permissions},
                                   {QStringLiteral("compatible"), plugin.apiVersion == kPluginApiVersion}});
    return plugins;
}

void PluginController::openFolder(const QString& pluginId) const
{
    const PluginManifest* plugin = findPlugin(pluginId);
    if (!plugin || !QDesktopServices::openUrl(QUrl::fromLocalFile(plugin->dir)))
        qCWarning(lcPlugin) << "could not open the folder of" << pluginId;
}

void PluginController::openPluginsFolder() const
{
    if (!QDir().mkpath(mPluginsDir) || !QDesktopServices::openUrl(QUrl::fromLocalFile(mPluginsDir)))
        qCWarning(lcPlugin) << "could not open the plugins folder" << mPluginsDir;
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
    if (!plugin)
        return {};
    if (plugin->apiVersion != kPluginApiVersion)
        return tr("%1 (incompatible)").arg(plugin->name);
    return plugin->name;
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
    if (!splitActionId(actionId, &pluginId, &commandId) || mRuns.contains(pluginId))
        return false;
    const PluginManifest* plugin = findPlugin(pluginId);
    return plugin && plugin->apiVersion == kPluginApiVersion;
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
        // The top of Favourites/Recents/Shared links: a listing, not a folder.
        if (entry.value(QStringLiteral("handle")).toULongLong() == 0 &&
            !entry.value(QStringLiteral("isRoot")).toBool())
            return false;
        selection.push_back({entry.value(QStringLiteral("name")).toString(),
                             entry.value(QStringLiteral("isFolder")).toBool()});
    }
    return pluginCommandAccepts(*command, selection);
}

void PluginController::execute(const QString& actionId,
                               const QVariantList& entries,
                               const QString& site)
{
    QString pluginId;
    QString commandId;
    if (!splitActionId(actionId, &pluginId, &commandId) || mRuns.contains(pluginId))
        return;
    const PluginManifest* plugin = findPlugin(pluginId);
    if (!plugin || plugin->apiVersion != kPluginApiVersion)
        return;

    // Re-read rather than taken from the row: the row has no parent, and a node
    // deleted since the menu opened is dropped instead of handed over.
    QJsonArray items;
    for (const QVariant& value : entries)
    {
        const QVariantMap entry = value.toMap();
        const std::optional<QJsonObject> item =
            entry.value(QStringLiteral("isRoot")).toBool()
                ? mHostApi.rootItem()
                : mHostApi.item(entry.value(QStringLiteral("handle")).toULongLong());
        if (item)
            items.append(*item);
    }
    const QJsonObject context{{QStringLiteral("site"), site},
                              {QStringLiteral("items"), items}};

    const auto command =
        std::find_if(plugin->commands.begin(), plugin->commands.end(), [&](const PluginCommand& c) {
            return c.id == commandId;
        });
    if (command == plugin->commands.end())
        return;
    const QString tempDir = QDir(mTempDir).filePath(QString::number(++mRunCount));
    auto* run = new PluginRun(*plugin, commandId, context, tempDir, &mHostApi, this);
    const QString pluginName = plugin->name;
    const QString commandTitle = command->title;
    const bool resultInDialog = command->resultInDialog;
    const qint64 startedAt = QDateTime::currentMSecsSinceEpoch();
    const int runId = mRunCount;
    connect(run,
            &PluginRun::finished,
            this,
            [this, runId, pluginId, pluginName, commandTitle, startedAt, resultInDialog](
                const QString& outcome, const QString& message, bool changed, const QStringList& deniedPermissions) {
                mRuns.remove(pluginId);
                removeConfirm(pluginId);
                ProgressState* state = findProgress(pluginId);
                // Cancelling is the user's own doing: nothing to report, wherever results go.
                const bool inDialog = resultInDialog && outcome != QStringLiteral("cancelled");
                if (inDialog)
                {
                    if (!state)
                    {
                        mProgress.push_back({runId, pluginId, pluginName, commandTitle, startedAt});
                        state = &mProgress.back();
                    }
                    state->finished = true;
                    state->preparing = false;
                    state->shown = true;
                    state->outcome = outcome;
                    state->result = message;
                    state->finishedAt = QDateTime::currentMSecsSinceEpoch();
                    emit progressRunsChanged();
                }
                else if (state)
                {
                    const bool wasShown = state->shown;
                    mProgress.erase(mProgress.begin() + (state - mProgress.data()));
                    if (wasShown)
                        emit progressRunsChanged();
                }
                ++mRunningRevision;
                emit runningChanged();
                emit commandFinished(pluginName, outcome, message, changed, inDialog);
                if (!deniedPermissions.isEmpty())
                    emit permissionsDenied(pluginName, deniedPermissions);
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
    // Every run gets the dialog eventually: it holds the only Cancel, and a command
    // without "progress" has no time limit either.
    mProgress.push_back({runId, pluginId, pluginName, commandTitle, startedAt});
    mProgress.back().show = command->progressShow;
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
                    state->rate.add(QDateTime::currentMSecsSinceEpoch(), current);
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
    const int showDelayMs = command->progress ? kProgressShowDelayMs : kUnrequestedProgressShowDelayMs;
    QTimer::singleShot(showDelayMs, this, [this, pluginId] {
        if (ProgressState* state = findProgress(pluginId))
        {
            state->shown = true;
            emit progressRunsChanged();
        }
    });
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
                                {QStringLiteral("forceStoppable"), state.forceStoppable},
                                {QStringLiteral("runId"), state.runId},
                                {QStringLiteral("finished"), state.finished},
                                {QStringLiteral("outcome"), state.outcome},
                                {QStringLiteral("result"), state.result},
                                {QStringLiteral("finishedAt"), state.finishedAt},
                                {QStringLiteral("show"), state.show},
                                // Items per second, or -1 while it cannot be told yet.
                                {QStringLiteral("rate"), state.rate.perSecond().value_or(-1.0)},
                                {QStringLiteral("rateAt"), state.rate.lastIncreaseAt()}});
    }
    return runs;
}

void PluginController::dismissResult(int runId)
{
    const auto state = std::find_if(mProgress.begin(), mProgress.end(), [&](const ProgressState& s) {
        return s.finished && s.runId == runId;
    });
    if (state == mProgress.end())
        return;
    mProgress.erase(state);
    emit progressRunsChanged();
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

void PluginController::forceStopAll()
{
    // Not mRuns: a run leaves it once it answers, while its process may live on
    // through the shutdown grace.
    const QList<PluginRun*> runs = findChildren<PluginRun*>(Qt::FindDirectChildrenOnly);
    for (PluginRun* run : runs)
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
        if (state.pluginId == pluginId && !state.finished)
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
