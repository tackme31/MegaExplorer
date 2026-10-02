#include "PluginRun.h"

#include "app/Logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QLocale>
#include <QPointer>
#include <QTimer>

#include <memory>
#include <utility>
#include <vector>
#include <windows.h>

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
// After this long without an answer to $/cancel the user is offered to kill the plugin.
constexpr int kCancelGraceMs = 10000;
constexpr int kCancelledCode = -32800;
constexpr int kInvalidParamsCode = -32602;

QString errorMessageOf(const QJsonObject& response)
{
    return response.value(QStringLiteral("error"))
        .toObject()
        .value(QStringLiteral("message"))
        .toString();
}

// What the user is shown for an error: error.message is one sentence by JSON-RPC's
// rule, so the full text, if any, comes in error.data.message.
QString errorTextOf(const QJsonObject& response)
{
    const QJsonObject error = response.value(QStringLiteral("error")).toObject();
    const QJsonValue full = error.value(QStringLiteral("data")).toObject().value(QStringLiteral("message"));
    return full.isString() && !full.toString().isEmpty() ? full.toString()
                                                         : error.value(QStringLiteral("message")).toString();
}

// Kills everything inside once the last handle closes -- including when this app
// dies -- so a plugin's own children (uv's python.exe) go with it.
HANDLE createKillOnCloseJob()
{
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        return nullptr;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
    {
        CloseHandle(job);
        return nullptr;
    }
    return job;
}

// PROC_THREAD_ATTRIBUTE_JOB_LIST puts the process in the job at creation, so it
// cannot start a child before being assigned. Must outlive QProcess::start().
struct JobStartupInfo
{
    STARTUPINFOEXW info{};
    std::vector<char> attributeBuffer;
    HANDLE job = nullptr;

    ~JobStartupInfo()
    {
        if (info.lpAttributeList)
            DeleteProcThreadAttributeList(info.lpAttributeList);
    }
};

std::shared_ptr<JobStartupInfo> makeJobStartupInfo(HANDLE job)
{
    auto startup = std::make_shared<JobStartupInfo>();
    startup->job = job;
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    startup->attributeBuffer.resize(size);
    auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(startup->attributeBuffer.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &size))
        return nullptr;
    startup->info.lpAttributeList = list;
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &startup->job,
                                   sizeof(HANDLE), nullptr, nullptr))
        return nullptr;
    return startup;
}
} // namespace

PluginRun::PluginRun(PluginManifest manifest,
                     QString commandId,
                     QJsonObject context,
                     QString tempDir,
                     const PluginHostApi* hostApi,
                     QObject* parent)
    : QObject(parent), mManifest(std::move(manifest)), mCommandId(std::move(commandId)),
      mContext(std::move(context)), mHostState{std::move(tempDir), {}}, mHostApi(hostApi), mKillTimer(new QTimer(this)), mInitTimer(new QTimer(this)),
      mCancelTimer(new QTimer(this))
{
    mCancelTimer->setSingleShot(true);
    mCancelTimer->setInterval(kCancelGraceMs);
    connect(mCancelTimer, &QTimer::timeout, this, [this] {
        if (mStage != Stage::Done)
            emit cancelIgnored();
    });
    mInitTimer->setSingleShot(true);
    mInitTimer->setInterval(kInitializeTimeoutMs);
    connect(mInitTimer, &QTimer::timeout, this, [this] {
        qCWarning(lcPlugin) << mManifest.id << "did not answer initialize in time; killing it";
        finish(QStringLiteral("timeout"), {});
        killAll();
    });
    mKillTimer->setSingleShot(true);
    mKillTimer->setInterval(kShutdownGraceMs);
    connect(mKillTimer, &QTimer::timeout, this, [this] {
        qCWarning(lcPlugin) << mManifest.id << "did not exit after shutdown; killing it";
        killAll();
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
                finish(mCancelRequested ? QStringLiteral("cancelled") : QStringLiteral("crashed"), {});
            removeTempDir();
            deleteLater();
        });
}

PluginRun::~PluginRun()
{
    // QProcess warns, and leaves the child running, if destroyed while it runs.
    if (mProcess.state() != QProcess::NotRunning)
    {
        mProcess.disconnect(this);
        killAll();
        mProcess.waitForFinished(1000);
    }
    if (mJob)
        CloseHandle(mJob);
    removeTempDir();
}

void PluginRun::removeTempDir()
{
    // QDir("") is the working directory: never let an empty path reach removeRecursively().
    if (!mHostState.tempDir.isEmpty())
        QDir(mHostState.tempDir).removeRecursively();
}

void PluginRun::killAll()
{
    if (mJob)
        TerminateJobObject(mJob, 1);
    else
        mProcess.kill();
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
    mJob = createKillOnCloseJob();
    if (std::shared_ptr<JobStartupInfo> startup = mJob ? makeJobStartupInfo(mJob) : nullptr)
    {
        mProcess.setCreateProcessArgumentsModifier(
            [startup](QProcess::CreateProcessArguments* args) {
                startup->info.StartupInfo = *args->startupInfo;
                startup->info.StartupInfo.cb = sizeof(STARTUPINFOEXW);
                args->startupInfo = &startup->info.StartupInfo;
                args->flags |= EXTENDED_STARTUPINFO_PRESENT;
            });
    }
    else
    {
        qCWarning(lcPlugin) << mManifest.id << "no job object; its child processes may outlive it";
    }
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

void PluginRun::cancel()
{
    if (mStage == Stage::Done || mCancelRequested)
        return;
    mCancelRequested = true;
    if (mStage == Stage::Initializing)
    {
        finish(QStringLiteral("cancelled"), {});
        killAll();
        return;
    }
    notify(QStringLiteral("$/cancel"), {{QStringLiteral("invocationId"), kInvocationId}});
    mCancelTimer->start();
}

void PluginRun::forceStop()
{
    if (mStage == Stage::Done)
        return;
    qCInfo(lcPlugin) << mManifest.id << "force-stopped by the user";
    finish(QStringLiteral("cancelled"), {});
    killAll();
}

void PluginRun::notify(const QString& method, const QJsonObject& params)
{
    const QJsonObject message{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                              {QStringLiteral("method"), method},
                              {QStringLiteral("params"), params}};
    mProcess.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
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
    const QString method = message.value(QStringLiteral("method")).toString();
    if (!message.contains(QStringLiteral("id")))
    {
        if (method == QLatin1String("ui.progress") && mStage == Stage::Executing)
        {
            const QJsonObject params = message.value(QStringLiteral("params")).toObject();
            const auto count = [&params](const char* key) -> qint64 {
                const QJsonValue value = params.value(QLatin1String(key));
                return value.isDouble() && value.toDouble() >= 0
                           ? static_cast<qint64>(value.toDouble())
                           : -1;
            };
            emit progressReported(count("current"),
                                  count("total"),
                                  params.value(QStringLiteral("message")).toString());
            return;
        }
        qCInfo(lcPlugin) << mManifest.id << "ignored notification" << method;
        return;
    }
    // The answer can arrive after this run is gone (a write still in flight when the
    // plugin died); the QPointer drops it then.
    const QJsonValue id = message.value(QStringLiteral("id"));
    if (method == QLatin1String("ui.confirm"))
    {
        handleConfirm(id, message.value(QStringLiteral("params")).toObject());
        return;
    }
    mHostApi->call(method,
                   message.value(QStringLiteral("params")).toObject(),
                   mHostState,
                   [self = QPointer<PluginRun>(this), id, method](const PluginHostApi::Reply& result) {
                       if (self)
                           self->writeReply(id, method, result);
                   });
}

void PluginRun::writeReply(const QJsonValue& id,
                           const QString& method,
                           const PluginHostApi::Reply& result)
{
    if (result.mutated)
        mChanged = true;
    if (mProcess.state() != QProcess::Running)
        return;
    QJsonObject reply{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}};
    if (result.errorCode)
    {
        qCInfo(lcPlugin) << mManifest.id << method << "failed:" << result.errorMessage;
        reply.insert(QStringLiteral("error"),
                     QJsonObject{{QStringLiteral("code"), *result.errorCode},
                                 {QStringLiteral("message"), result.errorMessage}});
    }
    else
    {
        reply.insert(QStringLiteral("result"), result.result);
    }
    mProcess.write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n');
}

void PluginRun::handleConfirm(const QJsonValue& id, const QJsonObject& params)
{
    const auto fail = [&](const QString& message) {
        writeReply(id, QStringLiteral("ui.confirm"), {{}, kInvalidParamsCode, message, false});
    };
    if (mStage != Stage::Executing)
        return fail(QStringLiteral("ui.confirm is only allowed during command.execute"));
    if (!mConfirmId.isUndefined())
        return fail(QStringLiteral("Another ui.confirm is still open"));
    const QJsonValue message = params.value(QStringLiteral("message"));
    const QJsonValue title = params.value(QStringLiteral("title"));
    const QJsonValue okLabel = params.value(QStringLiteral("okLabel"));
    const QJsonValue danger = params.value(QStringLiteral("danger"));
    const auto optionalString = [](const QJsonValue& v) { return v.isUndefined() || v.isNull() || v.isString(); };
    if (!message.isString() || message.toString().isEmpty() || !optionalString(title) ||
        !optionalString(okLabel) || !(danger.isUndefined() || danger.isNull() || danger.isBool()))
        return fail(QStringLiteral("ui.confirm takes {message, title?, okLabel?, danger?}"));
    mConfirmId = id;
    emit confirmRequested(title.toString(), message.toString(), okLabel.toString(), danger.toBool());
}

void PluginRun::answerConfirm(bool ok)
{
    if (mConfirmId.isUndefined())
        return;
    const QJsonValue id = std::exchange(mConfirmId, QJsonValue(QJsonValue::Undefined));
    writeReply(id, QStringLiteral("ui.confirm"), {QJsonObject{{QStringLiteral("ok"), ok}}, std::nullopt, {}, false});
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
        emit executionStarted();
        return;
    }
    if (id == kExecuteId && mStage == Stage::Executing)
    {
        const int errorCode =
            message.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt();
        if (isError && errorCode == kCancelledCode)
        {
            qCInfo(lcPlugin) << mManifest.id << mCommandId << "was cancelled";
            finish(QStringLiteral("cancelled"), {});
        }
        else if (isError)
        {
            const QString text = errorTextOf(message);
            qCInfo(lcPlugin).noquote() << mManifest.id << mCommandId << "returned an error:" << text;
            finish(QStringLiteral("error"), text);
        }
        else
        {
            // Logged in full: a toast shows only the first lines.
            const QString text =
                message.value(QStringLiteral("result")).toObject().value(QStringLiteral("message")).toString();
            if (!text.isEmpty())
                qCInfo(lcPlugin).noquote() << mManifest.id << mCommandId << "returned:" << text;
            finish(QStringLiteral("ok"), text);
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
    mCancelTimer->stop();
    mStage = Stage::Done;
    emit finished(outcome, message, mChanged);
}

void PluginRun::stopProcess()
{
    if (mProcess.state() == QProcess::NotRunning)
        return;
    send(kShutdownId, QStringLiteral("shutdown"), {});
    mKillTimer->start();
}
