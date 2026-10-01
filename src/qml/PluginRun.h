#pragma once
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
    // (run.command does not resolve), "failedToStart", or "crashed" (exited or
    // spoke garbage before answering). message is the plugin's text, if any.
    PluginRun(PluginManifest manifest,
              QString commandId,
              QJsonObject context,
              QObject* parent = nullptr);
    ~PluginRun() override;

    void start();

    const PluginManifest& manifest() const
    {
        return mManifest;
    }

signals:
    void finished(const QString& outcome, const QString& message);

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
    void finish(const QString& outcome, const QString& message);
    void stopProcess();

    PluginManifest mManifest;
    QString mCommandId;
    QJsonObject mContext;
    QProcess mProcess;
    QTimer* mKillTimer;
    QByteArray mStdoutBuffer;
    QByteArray mStderrBuffer;
    Stage mStage = Stage::Initializing;
};
