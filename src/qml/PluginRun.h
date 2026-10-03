#pragma once
#include "PluginHostApi.h"
#include "PluginManifest.h"

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>

class QTimer;

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
    // The user's answer to the open ui.confirm; ignored when none is open.
    void answerConfirm(bool ok);

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
    // From ui.progress; current and total are -1 when the plugin left them out.
    void progressReported(qint64 current, qint64 total, const QString& message);
    // Still running a while after cancel(); forceStop() is the way out.
    void cancelIgnored();
    // From ui.confirm; the plugin waits until answerConfirm(). title and okLabel are
    // empty when the plugin left them out.
    void confirmRequested(const QString& title, const QString& message, const QString& okLabel, bool danger);

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
    void handleConfirm(const QJsonValue& id, const QJsonObject& params);
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
    // The id of the ui.confirm awaiting the user; undefined when none is.
    // Not the default: a default QJsonValue is Null, which is a valid id.
    QJsonValue mConfirmId{QJsonValue::Undefined};
};
