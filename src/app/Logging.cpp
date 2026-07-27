#include "Logging.h"

#include <QDir>
#include <QFile>
#include <QMutex>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>
#include <cstdlib>

// Default-constructed (2-arg) Q_LOGGING_CATEGORY enables all message types --
// confirmed against Qt's own docs, since only categories with the "qt."
// prefix get the debug/info-suppressed default. lcSdk doesn't need a
// QtInfoMsg floor either: MegaApi::setLogLevel defaults to LOG_LEVEL_INFO, so
// the SDK itself never emits DEBUG/VERBOSE unless that's raised.
Q_LOGGING_CATEGORY(lcApp, "megaexplorer.app")
Q_LOGGING_CATEGORY(lcNavigation, "megaexplorer.navigation")
Q_LOGGING_CATEGORY(lcSearch, "megaexplorer.search")
Q_LOGGING_CATEGORY(lcDownload, "megaexplorer.download")
Q_LOGGING_CATEGORY(lcThumbnail, "megaexplorer.thumbnail")
Q_LOGGING_CATEGORY(lcSdk, "megaexplorer.sdk")

namespace
{

QMutex& logMutex()
{
    static QMutex m;
    return m;
}

// Static-local, not a member -- this file only ever needs one file sink for
// the process's lifetime; qInstallMessageHandler's C-function-pointer
// signature has no room for a context object anyway.
QFile& logFile()
{
    static QFile f;
    return f;
}

// Single-generation rotation: previous run's log becomes path+".1", anything
// older than that is simply gone. No size cap, no external library -- this
// is a desktop app for one user, not a service.
void rotateExistingLog(const QString& path)
{
    if (!QFile::exists(path))
        return;
    const QString backupPath = path + ".1";
    QFile::remove(backupPath); // ignore failure: may simply not exist yet
    QFile::rename(path, backupPath);
}

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    const QString formatted = qFormatLogMessage(type, context, msg);

    // qInstallMessageHandler's contract requires the handler to be
    // thread-safe: MegaSdkLogger forwards MEGA SDK callbacks that can fire
    // from an SDK-internal background thread, in addition to GUI-thread
    // qCWarning calls from controllers.
    QMutexLocker lock(&logMutex());
    fprintf(stderr, "%s\n", qPrintable(formatted)); // keeps Qt Creator's Application Output working
    if (logFile().isOpen())
    {
        QTextStream ts(&logFile());
        ts << formatted << Qt::endl;
        logFile().flush();
    }
    lock.unlock(); // don't hold the mutex across abort()

    if (type == QtFatalMsg)
        abort(); // installing a custom handler bypasses Qt's own abort-on-fatal
}

} // namespace

void installLogging()
{
    qSetMessagePattern(
        "[%{time yyyy-MM-dd HH:mm:ss.zzz}] [%{category}] [%{type}] %{message} (%{file}:%{line})");

    // AppLocalDataLocation, not AppDataLocation: on Windows the latter is the
    // *roaming* profile path, which would sync this log file over the
    // network on every logon/logoff in a domain environment for no benefit.
    // AppLocalDataLocation resolves to the same path as AppDataLocation on
    // macOS/Linux (Qt docs), so this only changes behavior on Windows.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    const QString path = dir + "/MegaExplorer.log";

    rotateExistingLog(path);
    logFile().setFileName(path);
    if (!logFile().open(QIODevice::WriteOnly | QIODevice::Text))
    {
        // No log-file destination, but stderr (via messageHandler below)
        // still works -- degrade rather than fail startup over this.
        fprintf(stderr, "installLogging: failed to open log file %s\n", qPrintable(path));
    }

    qInstallMessageHandler(messageHandler);
}
