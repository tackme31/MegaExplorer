#pragma once
#include "PluginHostApi.h"
#include "PluginManifest.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <memory>
#include <QtQml/qqmlregistration.h>
#include <vector>

class IMegaClient;
class PluginRun;
class QTimer;

// The installed plugins (each a folder holding plugin.json under pluginsDir) and
// running their commands. Menu IDs are "plugin:<pluginId>/<commandId>", which
// ActionCatalog.qml expands MenuAction::PluginCommands into.
class PluginController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided as the pluginController context property")

    // Bumped whenever a run starts or ends, so menu greying can re-evaluate.
    Q_PROPERTY(int runningRevision READ runningRevision NOTIFY runningChanged)
    // The plugin dialog's rows, oldest first: runs of "progress": true commands that
    // have been going for a moment, and finished runs of "result": "dialog" commands
    // until dismissResult(). Maps of runId, pluginId, pluginName, commandTitle,
    // startedAt (ms since epoch), preparing, current and total (-1 when unknown),
    // message (the progress text), cancelling, forceStoppable (cancelled but still
    // running after a grace period), and finished, outcome (as PluginRun::finished),
    // result (the plugin's message) and finishedAt.
    Q_PROPERTY(QVariantList progressRuns READ progressRuns NOTIFY progressRunsChanged)
    // Open ui.confirm questions, oldest first: maps of pluginId, pluginName, title,
    // message, okLabel and danger. Answered with answerConfirm().
    Q_PROPERTY(QVariantList confirmRequests READ confirmRequests NOTIFY confirmRequestsChanged)

public:
    // tempRoot holds the files fetched for plugins, one folder per app process
    // and run beneath it; leftovers of processes no longer running are removed here.
    PluginController(std::shared_ptr<IMegaClient> client,
                     QString pluginsDir,
                     const QString& tempRoot,
                     PluginHostApi::UserDownloads downloads = {},
                     QObject* parent = nullptr);

    QString pluginsDir() const
    {
        return mPluginsDir;
    }

    int runningRevision() const
    {
        return mRunningRevision;
    }

    QVariantList progressRuns() const;
    QVariantList confirmRequests() const;

    // Rescans pluginsDir. Plugins are kept in name order, which is menu order.
    Q_INVOKABLE void reload();

    // One map per loaded plugin, in menu order: id, name, version, description,
    // repositoryUrl, permissions (a string list) and compatible (apiVersion matches).
    Q_INVOKABLE QVariantList installedPlugins() const;
    // pluginId's folder in Explorer.
    Q_INVOKABLE void openFolder(const QString& pluginId) const;

    Q_INVOKABLE QStringList menuActionIds() const;
    Q_INVOKABLE bool isMenuActionId(const QString& actionId) const;
    // "plugin:<pluginId>": the submenu a command's row is folded into.
    Q_INVOKABLE QString groupOf(const QString& actionId) const;
    Q_INVOKABLE QString groupLabel(const QString& group) const;
    Q_INVOKABLE QString commandTitle(const QString& actionId) const;
    // False while the same plugin is already running a command: one run at a time.
    Q_INVOKABLE bool canRun(const QString& actionId) const;
    // The command's `when` against the selection; entries as for execute().
    Q_INVOKABLE bool accepts(const QString& actionId, const QVariantList& entries) const;

    // entries are FileListModel::selectedEntries() maps (handle, name, isFolder).
    Q_INVOKABLE void execute(const QString& actionId, const QVariantList& entries);
    Q_INVOKABLE void cancel(const QString& pluginId);
    Q_INVOKABLE void forceStop(const QString& pluginId);
    // Kills every run at once, without $/cancel: called on logout, after which a
    // run's handles would name nothing, or a node of the next account.
    Q_INVOKABLE void forceStopAll();
    Q_INVOKABLE void dismissResult(int runId);
    Q_INVOKABLE void answerConfirm(const QString& pluginId, bool ok);

signals:
    void runningChanged();
    void progressRunsChanged();
    void confirmRequestsChanged();
    // outcome and changed as PluginRun::finished. inDialog: the result is shown in
    // progressRuns, so no toast is wanted.
    void commandFinished(const QString& pluginName,
                         const QString& outcome,
                         const QString& message,
                         bool changed,
                         bool inDialog);
    // After commandFinished, once per run that was refused any undeclared permission,
    // however many calls were refused.
    void permissionsDenied(const QString& pluginName, const QStringList& permissions);
    // ui.reveal: open handle's folder in the current tab and select name there.
    void revealRequested(quint64 handle, const QString& name);

private:
    const PluginManifest* findPlugin(const QString& pluginId) const;
    const PluginCommand* findCommand(const QString& actionId) const;
    // Splits "plugin:<pluginId>/<commandId>"; false when actionId is not one.
    static bool splitActionId(const QString& actionId, QString* pluginId, QString* commandId);

    struct ProgressState
    {
        int runId = 0;
        QString pluginId;
        QString pluginName;
        QString commandTitle;
        qint64 startedAt = 0;
        bool preparing = true;
        qint64 current = -1;
        qint64 total = -1;
        QString message;
        bool cancelling = false;
        bool forceStoppable = false;
        bool shown = false;
        bool finished = false;
        QString outcome;
        QString result;
        qint64 finishedAt = 0;
    };
    // The row of pluginId's run in progress, not a finished one.
    ProgressState* findProgress(const QString& pluginId);
    // Coalesces updates: a plugin may report every item, the dialog only needs a few a second.
    void scheduleProgressUpdate();

    struct ConfirmState
    {
        QString pluginId;
        QString pluginName;
        QString title;
        QString message;
        QString okLabel;
        bool danger = false;
    };
    // Drops pluginId's open question, if any.
    void removeConfirm(const QString& pluginId);

    PluginHostApi mHostApi;
    std::vector<ProgressState> mProgress;
    std::vector<ConfirmState> mConfirms;
    QTimer* mProgressUpdateTimer;
    QString mPluginsDir;
    std::vector<PluginManifest> mPlugins;
    QHash<QString, PluginRun*> mRuns;
    int mRunningRevision = 0;
    QString mTempDir;
    int mRunCount = 0;
};
