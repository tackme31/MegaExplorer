#include "PluginRun.h"

#include "app/Logging.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QLocale>
#include <QTimer>

#include <utility>

namespace
{
constexpr int kInitializeId = 1;
constexpr int kExecuteId = 2;
constexpr int kShutdownId = 3;
// How long a plugin gets to exit on its own after shutdown before it is killed.
constexpr int kShutdownGraceMs = 5000;
// Long because uv may first fetch Python and the dependencies.
constexpr int kInitializeTimeoutMs = 5 * 60 * 1000;
constexpr int kInvocationId = 1;

QString errorMessageOf(const QJsonObject& response)
{
    return response.value(QStringLiteral("error"))
        .toObject()
        .value(QStringLiteral("message"))
        .toString();
}
} // namespace

PluginRun::PluginRun(PluginManifest manifest,
                     QString commandId,
                     QJsonObject context,
                     QObject* parent)
    : QObject(parent), mManifest(std::move(manifest)), mCommandId(std::move(commandId)),
      mContext(std::move(context)), mKillTimer(new QTimer(this)), mInitTimer(new QTimer(this))
{
    mInitTimer->setSingleShot(true);
    mInitTimer->setInterval(kInitializeTimeoutMs);
    connect(mInitTimer, &QTimer::timeout, this, [this] {
        qCWarning(lcPlugin) << mManifest.id << "did not answer initialize in time; killing it";
        finish(QStringLiteral("timeout"), {});
        mProcess.kill();
    });
    mKillTimer->setSingleShot(true);
    mKillTimer->setInterval(kShutdownGraceMs);
    connect(mKillTimer, &QTimer::timeout, this, [this] {
        qCWarning(lcPlugin) << mManifest.id << "did not exit after shutdown; killing it";
        mProcess.kill();
    });

    connect(&mProcess, &QProcess::readyReadStandardOutput, this, &PluginRun::readStdout);
    connect(&mProcess, &QProcess::readyReadStandardError, this, &PluginRun::readStderr);
    connect(&mProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
        {
            qCWarning(lcPlugin) << mManifest.id << "failed to start:" << mProcess.errorString();
            finish(QStringLiteral("failedToStart"), {});
            deleteLater();
        }
    });
    connect(
        &mProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
            mKillTimer->stop();
            readStderr();
            qCInfo(lcPlugin) << mManifest.id << "exited, code" << exitCode
                             << (status == QProcess::CrashExit ? "(crashed)" : "");
            if (mStage != Stage::Done)
                finish(QStringLiteral("crashed"), {});
            deleteLater();
        });
}

PluginRun::~PluginRun()
{
    // QProcess warns, and leaves the child running, if destroyed while it runs.
    if (mProcess.state() != QProcess::NotRunning)
    {
        mProcess.disconnect(this);
        mProcess.kill();
        mProcess.waitForFinished(1000);
    }
}

void PluginRun::start()
{
    const QString program = resolvePluginProgram(mManifest);
    if (program.isEmpty())
    {
        qCWarning(lcPlugin) << mManifest.id << "run.command not found:" << mManifest.command;
        // Queued so the caller has connected to finished() and registered the run.
        QMetaObject::invokeMethod(
            this,
            [this] {
                finish(QStringLiteral("notFound"), mManifest.command);
                deleteLater();
            },
            Qt::QueuedConnection);
        return;
    }

    qCInfo(lcPlugin) << mManifest.id << "starting" << program << mManifest.args;
    mProcess.setWorkingDirectory(mManifest.dir);
    mProcess.setProgram(program);
    mProcess.setArguments(mManifest.args);
    mProcess.start();
    mInitTimer->start();

    QJsonObject app{{QStringLiteral("version"), QCoreApplication::applicationVersion()},
                    {QStringLiteral("locale"), QLocale().bcp47Name()}};
    QJsonObject plugin{{QStringLiteral("id"), mManifest.id},
                       {QStringLiteral("version"), mManifest.version},
                       {QStringLiteral("dir"), mManifest.dir}};
    send(kInitializeId,
         QStringLiteral("initialize"),
         {{QStringLiteral("apiVersion"), 1},
          {QStringLiteral("app"), app},
          {QStringLiteral("plugin"), plugin}});
}

void PluginRun::send(int id, const QString& method, const QJsonObject& params)
{
    const QJsonObject message{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                              {QStringLiteral("id"), id},
                              {QStringLiteral("method"), method},
                              {QStringLiteral("params"), params}};
    mProcess.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}

void PluginRun::readStdout()
{
    mStdoutBuffer += mProcess.readAllStandardOutput();
    qsizetype newline;
    while ((newline = mStdoutBuffer.indexOf('\n')) >= 0)
    {
        const QByteArray line = mStdoutBuffer.left(newline).trimmed();
        mStdoutBuffer.remove(0, newline + 1);
        if (line.isEmpty())
            continue;
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if (!doc.isObject())
        {
            qCWarning(lcPlugin) << mManifest.id
                                << "wrote a non-JSON-RPC line to stdout:" << line.left(200);
            continue;
        }
        handleMessage(doc.object());
    }
}

void PluginRun::readStderr()
{
    mStderrBuffer += mProcess.readAllStandardError();
    qsizetype newline;
    while ((newline = mStderrBuffer.indexOf('\n')) >= 0)
    {
        const QByteArray line = mStderrBuffer.left(newline).trimmed();
        mStderrBuffer.remove(0, newline + 1);
        if (!line.isEmpty())
            qCInfo(lcPlugin).noquote()
                << QStringLiteral("[plugin:%1]").arg(mManifest.id) << QString::fromUtf8(line);
    }
}

void PluginRun::handleMessage(const QJsonObject& message)
{
    const bool isRequest = message.contains(QStringLiteral("method"));
    if (!isRequest)
    {
        handleResponse(message);
        return;
    }
    // No host methods yet: answer every request so the plugin is not left waiting.
    const QString method = message.value(QStringLiteral("method")).toString();
    if (!message.contains(QStringLiteral("id")))
    {
        qCInfo(lcPlugin) << mManifest.id << "ignored notification" << method;
        return;
    }
    qCInfo(lcPlugin) << mManifest.id << "called unsupported method" << method;
    const QJsonObject error{
        {QStringLiteral("code"), -32601},
        {QStringLiteral("message"), QStringLiteral("Method not found: %1").arg(method)}};
    const QJsonObject reply{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                            {QStringLiteral("id"), message.value(QStringLiteral("id"))},
                            {QStringLiteral("error"), error}};
    mProcess.write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n');
}

void PluginRun::handleResponse(const QJsonObject& message)
{
    const int id = message.value(QStringLiteral("id")).toInt(-1);
    const bool isError = message.contains(QStringLiteral("error"));

    if (id == kInitializeId && mStage == Stage::Initializing)
    {
        if (isError)
        {
            qCWarning(lcPlugin) << mManifest.id << "initialize failed:" << errorMessageOf(message);
            finish(QStringLiteral("error"), errorMessageOf(message));
            stopProcess();
            return;
        }
        mInitTimer->stop();
        mStage = Stage::Executing;
        send(kExecuteId,
             QStringLiteral("command.execute"),
             {{QStringLiteral("invocationId"), kInvocationId},
              {QStringLiteral("commandId"), mCommandId},
              {QStringLiteral("context"), mContext}});
        return;
    }
    if (id == kExecuteId && mStage == Stage::Executing)
    {
        if (isError)
        {
            qCInfo(lcPlugin) << mManifest.id << mCommandId
                             << "returned an error:" << errorMessageOf(message);
            finish(QStringLiteral("error"), errorMessageOf(message));
        }
        else
        {
            const QJsonObject result = message.value(QStringLiteral("result")).toObject();
            finish(QStringLiteral("ok"), result.value(QStringLiteral("message")).toString());
        }
        stopProcess();
        return;
    }
    if (id == kShutdownId)
    {
        mProcess.closeWriteChannel();
        return;
    }
    qCWarning(lcPlugin) << mManifest.id << "sent an unexpected response, id" << id;
}

void PluginRun::finish(const QString& outcome, const QString& message)
{
    if (mStage == Stage::Done)
        return;
    mInitTimer->stop();
    mStage = Stage::Done;
    emit finished(outcome, message);
}

void PluginRun::stopProcess()
{
    if (mProcess.state() == QProcess::NotRunning)
        return;
    send(kShutdownId, QStringLiteral("shutdown"), {});
    mKillTimer->start();
}
