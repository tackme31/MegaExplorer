#pragma once
#include "PluginHostApi.h"
#include "PluginManifest.h"
#include "PluginRun.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <memory>
#include <QtQml/qqmlregistration.h>
#include <vector>

class IMegaClient;
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
    // The progress dialog's rows, oldest first: one per run whose plugin has it open.
    // Maps of pluginId, pluginName, title (the command's title unless the plugin
    // gave one), message, value (0..1, or -1 for an indeterminate bar), detail,
    // cancelling, and forceStoppable (cancelled but still running after a grace period).
    Q_PROPERTY(QVariantList progressRuns READ progressRuns NOTIFY progressRunsChanged)
    // ui.alert / ui.confirm / ui.showResult in arrival order; only the first is shown.
    // Maps of seq, pluginName, kind ("alert", "confirm", "result"), title, message,
    // okLabel, danger and level (a result's "info"/"warning"/"error"). Answered or
    // closed with answerDialog().
    Q_PROPERTY(QVariantList dialogRequests READ dialogRequests NOTIFY dialogRequestsChanged)

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
    QVariantList dialogRequests() const;

    // What ui.selection reads; see PluginHostApi::setSelection.
    void setSelection(PluginHostApi::Selection selection)
    {
        mHostApi.setSelection(std::move(selection));
    }

    void setThumbnails(PluginHostApi::Thumbnails thumbnails)
    {
        mHostApi.setThumbnails(std::move(thumbnails));
    }

    // Rescans pluginsDir. Plugins are kept in name order, which is menu order.
    Q_INVOKABLE void reload();

    // One map per loaded plugin, in menu order: id, name, version, description,
    // repositoryUrl, permissions (a string list) and compatible (apiVersion matches).
    Q_INVOKABLE QVariantList installedPlugins() const;
    // pluginId's folder in Explorer.
    Q_INVOKABLE void openFolder(const QString& pluginId) const;
    // The folder plugins are discovered in, created first if it is not there yet.
    Q_INVOKABLE void openPluginsFolder() const;

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

    // entries are FileListModel::selectedEntries() maps (handle, name, isFolder), or
    // the one folder of a background/folder-row menu, whose isRoot names the Cloud
    // Drive root. site becomes context.site: "selection" or "folder".
    Q_INVOKABLE void execute(const QString& actionId,
                             const QVariantList& entries,
                             const QString& site = QStringLiteral("selection"));
    Q_INVOKABLE void cancel(const QString& pluginId);
    Q_INVOKABLE void forceStop(const QString& pluginId);
    // Kills every run at once, without $/cancel: called on logout, after which a
    // run's handles would name nothing, or a node of the next account.
    Q_INVOKABLE void forceStopAll();
    // Takes request seq off dialogRequests; for an alert or confirm, ok is the answer.
    Q_INVOKABLE void answerDialog(int seq, bool ok);

signals:
    void runningChanged();
    void progressRunsChanged();
    void dialogRequestsChanged();
    // outcome and changed as PluginRun::finished.
    void commandFinished(const QString& pluginName, const QString& outcome, const QString& message, bool changed);
    // ui.toast; level is "info", "warning" or "error".
    void toastRequested(const QString& pluginName, const QString& message, const QString& level);
    // After commandFinished, once per run that was refused any undeclared permission,
    // however many calls were refused.
    void permissionsDenied(const QString& pluginName, const QStringList& permissions);
    // ui.reveal: open handle's folder in the current tab and select name there.
    void revealRequested(quint64 handle, const QString& name);
    // ui.search: the facets as SearchFilter's enum values, the form
    // FolderNavigationController::replaceSearch takes.
    void searchRequested(const QString& query,
                         int nodeType,
                         int category,
                         int createdWithin,
                         bool favouritesOnly,
                         bool thisFolderOnly);
    // ui.open: a file the in-app viewer can show.
    void openRequested(quint64 handle, const QString& name, qulonglong sizeBytes);
    // ui.navigate: a Cloud Drive folder; a new tab opens in the background, as "Open in new tab" does.
    void navigateRequested(quint64 handle, bool isRoot, bool newTab);

private:
    const PluginManifest* findPlugin(const QString& pluginId) const;
    const PluginCommand* findCommand(const QString& actionId) const;
    // Splits "plugin:<pluginId>/<commandId>"; false when actionId is not one.
    static bool splitActionId(const QString& actionId, QString* pluginId, QString* commandId);

    // One per run in progress, whether its dialog is open or not.
    struct ProgressState
    {
        QString pluginId;
        QString pluginName;
        QString commandTitle;
        bool open = false;
        PluginProgressContent content;
        bool cancelling = false;
        bool forceStoppable = false;
    };
    ProgressState* findProgress(const QString& pluginId);
    // Coalesces updates: a plugin may report every item, the dialog only needs a few a second.
    void scheduleProgressUpdate();

    struct DialogState
    {
        int seq = 0;
        int runId = 0;
        QString pluginId;
        QString pluginName;
        PluginDialogRequest request;
    };
    // Drops runId's unanswered alerts and confirms; its results stay.
    void removeOpenDialogs(int runId);

    PluginHostApi mHostApi;
    std::vector<ProgressState> mProgress;
    std::vector<DialogState> mDialogs;
    int mNextDialogSeq = 1;
    QTimer* mProgressUpdateTimer;
    QString mPluginsDir;
    std::vector<PluginManifest> mPlugins;
    QHash<QString, PluginRun*> mRuns;
    int mRunningRevision = 0;
    QString mTempDir;
    int mRunCount = 0;
};
