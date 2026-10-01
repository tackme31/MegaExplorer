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
    // spoke garbage before answering). message is the plugin's text, if any.
    PluginRun(PluginManifest manifest,
              QString commandId,
              QJsonObject context,
              const PluginHostApi* hostApi,
              QObject* parent = nullptr);
    ~PluginRun() override;

    void start();

    const PluginManifest& manifest() const
    {
        return mManifest;
    }

signals:
    // changed: whether any write reached the account, so the view is worth re-reading.
    void finished(const QString& outcome, const QString& message, bool changed);
    // initialize was answered and command.execute sent.
    void executionStarted();
    // From ui.progress; current and total are -1 when the plugin left them out.
    void progressReported(qint64 current, qint64 total, const QString& message);

private:
    enum class Stage
    {
        Initializing,
        Executing,
        // Reported; the process may still be shutting down.
        Done,
    };

    void send(int id, const QString& method, const QJsonObject& params);
    void readStdout();
    void readStderr();
    void handleMessage(const QJsonObject& message);
    void handleResponse(const QJsonObject& message);
    void writeReply(const QJsonValue& id, const QString& method, const PluginHostApi::Reply& result);
    void finish(const QString& outcome, const QString& message);
    void stopProcess();
    // The process and everything it started.
    void killAll();

    PluginManifest mManifest;
    QString mCommandId;
    QJsonObject mContext;
    // Owned by PluginController, this run's parent. Not used once the run is being destroyed.
    const PluginHostApi* mHostApi;
    QProcess mProcess;
    QTimer* mKillTimer;
    QTimer* mInitTimer;
    // A Win32 HANDLE; void* keeps <windows.h> out of this header.
    void* mJob = nullptr;
    QByteArray mStdoutBuffer;
    QByteArray mStderrBuffer;
    Stage mStage = Stage::Initializing;
    bool mChanged = false;
};
