#pragma once
#include "PluginHostApi.h"
#include "PluginManifest.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <QtQml/qqmlregistration.h>
#include <memory>
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
    // Runs of "progress": true commands that have been going for a moment, oldest first:
    // maps of pluginName, commandTitle, startedAt (ms since epoch), preparing,
    // current and total (-1 when unknown), message.
    Q_PROPERTY(QVariantList progressRuns READ progressRuns NOTIFY progressRunsChanged)

public:
    PluginController(std::shared_ptr<IMegaClient> client,
                     QString pluginsDir,
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

    // Rescans pluginsDir. Plugins are kept in name order, which is menu order.
    Q_INVOKABLE void reload();

    Q_INVOKABLE QStringList menuActionIds() const;
    Q_INVOKABLE bool isMenuActionId(const QString& actionId) const;
    // "plugin:<pluginId>": the submenu a command's row is folded into.
    Q_INVOKABLE QString groupOf(const QString& actionId) const;
    Q_INVOKABLE QString groupLabel(const QString& group) const;
    Q_INVOKABLE QString commandTitle(const QString& actionId) const;
    // False while the same plugin is already running a command: one run at a time.
    Q_INVOKABLE bool canRun(const QString& actionId) const;

    // entries are FileListModel::selectedEntries() maps (handle, name, isFolder).
    Q_INVOKABLE void execute(const QString& actionId, const QVariantList& entries);

signals:
    void runningChanged();
    void progressRunsChanged();
    // outcome and changed as PluginRun::finished.
    void commandFinished(const QString& pluginName,
                         const QString& outcome,
                         const QString& message,
                         bool changed);

private:
    const PluginManifest* findPlugin(const QString& pluginId) const;
    // Splits "plugin:<pluginId>/<commandId>"; false when actionId is not one.
    static bool splitActionId(const QString& actionId, QString* pluginId, QString* commandId);

    struct ProgressState
    {
        QString pluginId;
        QString pluginName;
        QString commandTitle;
        qint64 startedAt = 0;
        bool preparing = true;
        qint64 current = -1;
        qint64 total = -1;
        QString message;
        bool shown = false;
    };
    ProgressState* findProgress(const QString& pluginId);
    // Coalesces updates: a plugin may report every item, the dialog only needs a few a second.
    void scheduleProgressUpdate();

    PluginHostApi mHostApi;
    std::vector<ProgressState> mProgress;
    QTimer* mProgressUpdateTimer;
    QString mPluginsDir;
    std::vector<PluginManifest> mPlugins;
    QHash<QString, PluginRun*> mRuns;
    int mRunningRevision = 0;
};
