#pragma once
#include "PluginHostApi.h"
#include "PluginManifest.h"

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>

#include <map>

class QTimer;

// What the progress dialog shows for one run (ui.showProgress / ui.updateProgress).
// Every call carries all of it: a field left out is not shown.
struct PluginProgressContent
{
    // Empty shows the command's title.
    QString title;
    QString message;
    // 0..1, or negative for an indeterminate bar.
    double value = -1;
    QString detail;
};

// A ui.alert, ui.confirm or ui.showResult for the dialog queue.
struct PluginDialogRequest
{
    // What answerDialog() takes; 0 for a result, which nobody waits on.
    int token = 0;
    // "alert", "confirm" or "result".
    QString kind;
    QString title;
    QString message;
    QString okLabel;
    bool danger = false;
    // A result's "info", "warning" or "error".
    QString level;
};

// One command execution: starts the plugin's process, runs initialize ->
// command.execute -> shutdown over NDJSON JSON-RPC on stdio, and reports once.
// The process lives only as long as this run.
class PluginRun : public QObject
{
    Q_OBJECT

public:
    // outcome is "ok", "error" (the plugin answered with an error), "notFound"
    // (run.command does not resolve), "failedToStart", "timeout"
    // (no answer to initialize), or "crashed" (exited or
    // spoke garbage before answering), or "cancelled". message is the plugin's text, if any.
    PluginRun(PluginManifest manifest,
              QString commandId,
              QJsonObject context,
              QString tempDir,
              const PluginHostApi* hostApi,
              QObject* parent = nullptr);
    ~PluginRun() override;

    void start();
    // Asks the plugin to stop ($/cancel); before command.execute it is simply killed.
    void cancel();
    // Kills the process, also one still in its shutdown grace after the result;
    // for a plugin that ignores cancel(), and on sign-out.
    void forceStop();
    // The user's answer to an open ui.alert / ui.confirm; ignored once it is gone.
    void answerDialog(int token, bool ok);

    const PluginManifest& manifest() const
    {
        return mManifest;
    }

signals:
    // changed: whether any write reached the account, so the view is worth re-reading.
    // deniedPermissions: those the plugin called for without declaring them.
    void finished(const QString& outcome, const QString& message, bool changed, const QStringList& deniedPermissions);
    // initialize was answered and command.execute sent.
    void executionStarted();
    // The progress dialog opened, closed or changed its content; content is empty when closed.
    void progressChanged(bool open, const PluginProgressContent& content);
    // Still running a while after cancel(); forceStop() is the way out.
    void cancelIgnored();
    // An alert or confirm waits for answerDialog(); a result is already answered.
    void dialogRequested(const PluginDialogRequest& request);
    // level is "info", "warning" or "error".
    void toastRequested(const QString& message, const QString& level);

private:
    enum class Stage
    {
        Initializing,
        Executing,
        // Reported; the process may still be shutting down.
        Done,
    };

    void send(int id, const QString& method, const QJsonObject& params);
    void notify(const QString& method, const QJsonObject& params);
    void readStdout();
    void readStderr();
    void handleMessage(const QJsonObject& message);
    void handleResponse(const QJsonObject& message);
    // ui.showProgress / hideProgress / alert / confirm / showResult / toast.
    void handleDialogRequest(const QJsonValue& id, const QString& method, const QJsonObject& params);
    void handleProgressUpdate(const QJsonObject& params);
    void writeReply(const QJsonValue& id, const QString& method, const PluginHostApi::Reply& result);
    void finish(const QString& outcome, const QString& message);
    void stopProcess();
    // The process and everything it started.
    void killAll();
    void removeTempDir();

    PluginManifest mManifest;
    QString mCommandId;
    QJsonObject mContext;
    // Its tempDir holds files fetched for the plugin; removed once the process has exited.
    PluginHostApi::RunState mHostState;
    // Owned by PluginController, this run's parent. Not used once the run is being destroyed.
    const PluginHostApi* mHostApi;
    QProcess mProcess;
    QTimer* mKillTimer;
    QTimer* mInitTimer;
    QTimer* mCancelTimer;
    // A Win32 HANDLE; void* keeps <windows.h> out of this header.
    void* mJob = nullptr;
    QByteArray mStdoutBuffer;
    QByteArray mStderrBuffer;
    Stage mStage = Stage::Initializing;
    bool mChanged = false;
    bool mCancelRequested = false;
    bool mProgressOpen = false;
    // Alerts and confirms awaiting the user, by token: the request's id and method.
    std::map<int, std::pair<QJsonValue, QString>> mOpenDialogs;
    int mNextDialogToken = 1;
    // When start() ran, sent to the plugin as command.execute's startedAt.
    qint64 mStartedAt = 0;
};
