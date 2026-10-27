#include "PluginRun.h"

#include "app/Logging.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QLocale>
#include <QPointer>
#include <QStyleHints>
#include <QTimer>

#include <algorithm>
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
// A dialog call that does not fit the dialogs' state: ui.showProgress while one is
// open, or any dialog after $/cancel.
constexpr int kInvalidStateCode = -32005;

bool isOptionalString(const QJsonValue& value)
{
    return value.isUndefined() || value.isNull() || value.isString();
}

bool isLevel(const QJsonValue& value)
{
    return value.isUndefined() || value.isNull() || value == QStringLiteral("info") ||
           value == QStringLiteral("warning") || value == QStringLiteral("error");
}

QString levelOf(const QJsonValue& value)
{
    return value.isString() ? value.toString() : QStringLiteral("info");
}

// ui.showProgress / ui.updateProgress params. A field of the wrong type is left out
// and named in badFields.
PluginProgressContent readProgressContent(const QJsonObject& params, QStringList* badFields)
{
    PluginProgressContent content;
    const auto readString = [&](const char* key, QString* out) {
        const QJsonValue value = params.value(QLatin1String(key));
        if (value.isString())
            *out = value.toString();
        else if (!value.isUndefined() && !value.isNull())
            *badFields << QLatin1String(key);
    };
    readString("title", &content.title);
    readString("message", &content.message);
    readString("detail", &content.detail);
    const QJsonValue value = params.value(QStringLiteral("value"));
    if (value.isDouble())
        content.value = std::clamp(value.toDouble(), 0.0, 1.0);
    else if (!value.isUndefined() && !value.isNull())
        *badFields << QStringLiteral("value");
    return content;
}

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
      mContext(std::move(context)), mHostState{std::move(tempDir)}, mHostApi(hostApi), mKillTimer(new QTimer(this)), mInitTimer(new QTimer(this)),
      mCancelTimer(new QTimer(this))
{
    mHostState.permissions = mManifest.permissions;
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
            PluginHostApi::abandonTransfers(mHostState);
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
    PluginHostApi::abandonTransfers(mHostState);
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
        // The process starts outside the job then, so killAll() must fall back to QProcess::kill().
        if (mJob)
            CloseHandle(std::exchange(mJob, nullptr));
    }
    mStartedAt = QDateTime::currentMSecsSinceEpoch();
    mProcess.start();
    mInitTimer->start();
    const auto command = std::find_if(mManifest.commands.begin(), mManifest.commands.end(), [this](const PluginCommand& c) {
        return c.id == mCommandId;
    });
    if (command != mManifest.commands.end() && command->progressDialog)
    {
        mProgressOpen = true;
        emit progressChanged(true, {});
    }

    QJsonObject app{{QStringLiteral("version"), QCoreApplication::applicationVersion()},
                    {QStringLiteral("locale"), QLocale().bcp47Name()}};
    // The scheme in effect: the settings choice or the env override, else the OS's.
    switch (QGuiApplication::styleHints()->colorScheme())
    {
        case Qt::ColorScheme::Light:
            app.insert(QStringLiteral("colorScheme"), QStringLiteral("light"));
            break;
        case Qt::ColorScheme::Dark:
            app.insert(QStringLiteral("colorScheme"), QStringLiteral("dark"));
            break;
        case Qt::ColorScheme::Unknown:
            break;
    }
    QJsonObject plugin{{QStringLiteral("id"), mManifest.id},
                       {QStringLiteral("version"), mManifest.version},
                       {QStringLiteral("dir"), mManifest.dir}};
    send(kInitializeId,
         QStringLiteral("initialize"),
         {{QStringLiteral("apiVersion"), kPluginApiVersion},
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
    PluginHostApi::cancelTransfers(mHostState);
    mCancelTimer->start();
}

void PluginRun::forceStop()
{
    // A run that has answered can still be running through its shutdown grace.
    if (mStage == Stage::Done && mProcess.state() == QProcess::NotRunning)
        return;
    qCInfo(lcPlugin) << mManifest.id << "force-stopped";
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
        if (method == QLatin1String("ui.updateProgress") && mStage == Stage::Executing)
        {
            handleProgressUpdate(message.value(QStringLiteral("params")).toObject());
            return;
        }
        qCInfo(lcPlugin) << mManifest.id << "ignored notification" << method;
        return;
    }
    // The answer can arrive after this run is gone (a write still in flight when the
    // plugin died); the QPointer drops it then.
    const QJsonValue id = message.value(QStringLiteral("id"));
    // After the result the run's changes are already reported, and on sign-out the
    // next account's nodes could be reached; before execute there is no command yet.
    if (mStage != Stage::Executing)
    {
        writeReply(id, method, {{}, kInvalidParamsCode, QStringLiteral("%1 is only allowed during command.execute").arg(method), false});
        return;
    }
    static const QStringList dialogMethods{QStringLiteral("ui.showProgress"), QStringLiteral("ui.hideProgress"),
                                           QStringLiteral("ui.alert"),        QStringLiteral("ui.confirm"),
                                           QStringLiteral("ui.showResult"),   QStringLiteral("ui.toast")};
    if (dialogMethods.contains(method))
    {
        handleDialogRequest(id, method, message.value(QStringLiteral("params")).toObject());
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
        QJsonObject error{{QStringLiteral("code"), *result.errorCode},
                          {QStringLiteral("message"), result.errorMessage}};
        if (!result.errorData.isUndefined())
            error.insert(QStringLiteral("data"), result.errorData);
        reply.insert(QStringLiteral("error"), error);
    }
    else
    {
        reply.insert(QStringLiteral("result"), result.result);
    }
    mProcess.write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n');
}

void PluginRun::handleDialogRequest(const QJsonValue& id, const QString& method, const QJsonObject& params)
{
    const auto reply = [&](const QJsonObject& result) {
        writeReply(id, method, {result, std::nullopt, {}, false});
    };
    const auto fail = [&](int code, const QString& message) {
        writeReply(id, method, {{}, code, message, false});
    };

    if (method == QLatin1String("ui.hideProgress"))
    {
        // After Cancel the dialog stays up as "Cancelling…" until the run ends.
        if (mProgressOpen && !mCancelRequested)
        {
            mProgressOpen = false;
            emit progressChanged(false, {});
        }
        return reply({});
    }
    if (mCancelRequested)
        return fail(kInvalidStateCode, QStringLiteral("%1 was refused: the run is being cancelled").arg(method));

    if (method == QLatin1String("ui.showProgress"))
    {
        QStringList badFields;
        const PluginProgressContent content = readProgressContent(params, &badFields);
        if (!badFields.isEmpty())
            return fail(kInvalidParamsCode,
                        QStringLiteral("ui.showProgress takes {title?, message?, value? (a number), detail?}; bad: %1")
                            .arg(badFields.join(QStringLiteral(", "))));
        if (mProgressOpen)
            return fail(kInvalidStateCode,
                        QStringLiteral("The progress dialog is already open; use ui.updateProgress"));
        mProgressOpen = true;
        emit progressChanged(true, content);
        return reply({});
    }

    const QJsonValue message = params.value(QStringLiteral("message"));
    const QJsonValue title = params.value(QStringLiteral("title"));
    const QJsonValue okLabel = params.value(QStringLiteral("okLabel"));
    const QJsonValue danger = params.value(QStringLiteral("danger"));
    const QJsonValue kind = params.value(QStringLiteral("kind"));
    const bool hasMessage = message.isString() && !message.toString().isEmpty();

    if (method == QLatin1String("ui.toast"))
    {
        if (!hasMessage || !isLevel(kind))
            return fail(kInvalidParamsCode, QStringLiteral("ui.toast takes {message, kind?: \"info\"/\"warning\"/\"error\"}"));
        emit toastRequested(message.toString(), levelOf(kind));
        return reply({});
    }
    if (method == QLatin1String("ui.showResult"))
    {
        if (!hasMessage || !isOptionalString(title) || !isLevel(kind))
            return fail(kInvalidParamsCode,
                        QStringLiteral("ui.showResult takes {message, title?, kind?: \"info\"/\"warning\"/\"error\"}"));
        qCInfo(lcPlugin).noquote() << mManifest.id << mCommandId << "showed a result:" << message.toString();
        emit dialogRequested({0, QStringLiteral("result"), title.toString(), message.toString(), {}, false, levelOf(kind)});
        return reply({});
    }

    const bool isConfirm = method == QLatin1String("ui.confirm");
    if (!hasMessage || !isOptionalString(title) || !isOptionalString(okLabel) ||
        (isConfirm && !(danger.isUndefined() || danger.isNull() || danger.isBool())))
        return fail(kInvalidParamsCode,
                    isConfirm ? QStringLiteral("ui.confirm takes {message, title?, okLabel?, danger?}")
                              : QStringLiteral("ui.alert takes {message, title?, okLabel?}"));
    const int token = mNextDialogToken++;
    mOpenDialogs.emplace(token, std::make_pair(id, method));
    emit dialogRequested({token,
                          isConfirm ? QStringLiteral("confirm") : QStringLiteral("alert"),
                          title.toString(),
                          message.toString(),
                          okLabel.toString(),
                          isConfirm && danger.toBool(),
                          {}});
}

void PluginRun::handleProgressUpdate(const QJsonObject& params)
{
    if (!mProgressOpen || mCancelRequested)
    {
        if (!mProgressOpen)
            qCInfo(lcPlugin) << mManifest.id << "sent ui.updateProgress with no progress dialog open; ignored";
        return;
    }
    QStringList badFields;
    const PluginProgressContent content = readProgressContent(params, &badFields);
    if (!badFields.isEmpty())
        qCInfo(lcPlugin) << mManifest.id << "ui.updateProgress: ignored bad fields" << badFields;
    emit progressChanged(true, content);
}

void PluginRun::answerDialog(int token, bool ok)
{
    const auto open = mOpenDialogs.find(token);
    if (open == mOpenDialogs.end())
        return;
    const auto [id, method] = open->second;
    mOpenDialogs.erase(open);
    writeReply(id,
               method,
               {method == QLatin1String("ui.confirm") ? QJsonObject{{QStringLiteral("ok"), ok}} : QJsonObject{},
                std::nullopt,
                {},
                false});
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
              {QStringLiteral("context"), mContext},
              {QStringLiteral("startedAt"), static_cast<double>(mStartedAt)}});
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
    if (!mHostState.denied.isEmpty())
        qCWarning(lcPlugin) << mManifest.id << "was refused undeclared permissions" << mHostState.denied;
    emit finished(outcome, message, mChanged, mHostState.denied);
}

void PluginRun::stopProcess()
{
    if (mProcess.state() == QProcess::NotRunning)
        return;
    send(kShutdownId, QStringLiteral("shutdown"), {});
    mKillTimer->start();
}
