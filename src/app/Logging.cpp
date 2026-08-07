#include "Logging.h"

#include <QDir>
#include <QFile>
#include <QMutex>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>
#include <cstdlib>
#include <windows.h>

// The 2-arg Q_LOGGING_CATEGORY enables all message types; only "qt."-prefixed
// categories get Qt's debug/info-suppressed default.
Q_LOGGING_CATEGORY(lcApp, "megaexplorer.app")
Q_LOGGING_CATEGORY(lcNavigation, "megaexplorer.navigation")
Q_LOGGING_CATEGORY(lcSearch, "megaexplorer.search")
Q_LOGGING_CATEGORY(lcDownload, "megaexplorer.download")
Q_LOGGING_CATEGORY(lcUpload, "megaexplorer.upload")
Q_LOGGING_CATEGORY(lcThumbnail, "megaexplorer.thumbnail")
Q_LOGGING_CATEGORY(lcSdk, "megaexplorer.sdk")
Q_LOGGING_CATEGORY(lcSession, "megaexplorer.session")
Q_LOGGING_CATEGORY(lcAuth, "megaexplorer.auth")
Q_LOGGING_CATEGORY(lcQuickAccess, "megaexplorer.quickaccess")
Q_LOGGING_CATEGORY(lcFileOps, "megaexplorer.fileops")
Q_LOGGING_CATEGORY(lcAccount, "megaexplorer.account")

namespace
{

QMutex& logMutex()
{
    static QMutex m;
    return m;
}

// Static-local: qInstallMessageHandler takes a plain function pointer, so there
// is nowhere to hang a context object.
QFile& logFile()
{
    static QFile f;
    return f;
}

void rotateExistingLog(const QString& path)
{
    if (!QFile::exists(path))
        return;
    const QString backupPath = path + ".1";
    QFile::remove(backupPath); // ignore failure: may simply not exist yet
    QFile::rename(path, backupPath);
}

// Qt Creator's Application Output pane paints the whole stderr stream red
// regardless of content, so the split buys two tiers; warnings additionally
// carry an ANSI color code to separate them from errors.
FILE* streamFor(QtMsgType type)
{
    return (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) ? stderr : stdout;
}

// Deliberately not gated on _isatty: the main consumer is Qt Creator's
// Application Output, which decodes SGR codes but is a pipe, not a tty.
const char* colorCodeFor(QtMsgType type)
{
    switch (type)
    {
        case QtWarningMsg:
            return "\x1b[33m"; // yellow
        case QtCriticalMsg:
        case QtFatalMsg:
            return "\x1b[31m"; // red
        default:
            return nullptr;
    }
}
constexpr const char* kAnsiReset = "\x1b[0m";

// Legacy conhost.exe renders SGR color codes only if
// ENABLE_VIRTUAL_TERMINAL_PROCESSING is opted in per handle. GetConsoleMode
// fails when the handle is a pipe rather than a console (Qt Creator's
// Application Output), so this affects real terminals only.
void enableAnsiOnStream(DWORD stdHandle)
{
    const HANDLE h = GetStdHandle(stdHandle);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD mode = 0;
    if (!GetConsoleMode(h, &mode))
        return;
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    const QString formatted = qFormatLogMessage(type, context, msg);

    // The handler must be thread-safe: MegaSdkLogger forwards SDK callbacks from
    // an SDK-internal background thread.
    QMutexLocker lock(&logMutex());
    FILE* const stream = streamFor(type);
    if (const char* const color = colorCodeFor(type))
        fprintf(stream, "%s%s%s\n", color, qPrintable(formatted), kAnsiReset);
    else
        fprintf(stream, "%s\n", qPrintable(formatted));
    // stdout is block-buffered whenever it isn't a console, and MSVC's _IOLBF is
    // an alias for full buffering -- so flush per line or lines surface only at exit.
    fflush(stream);
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

    enableAnsiOnStream(STD_OUTPUT_HANDLE);
    enableAnsiOnStream(STD_ERROR_HANDLE);

    // AppLocalDataLocation, not AppDataLocation: on Windows the latter is the
    // roaming profile, which would sync this log over the network on every logon.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    const QString path = dir + "/MegaExplorer.log";

    rotateExistingLog(path);
    logFile().setFileName(path);
    if (!logFile().open(QIODevice::WriteOnly | QIODevice::Text))
    {
        fprintf(stderr, "installLogging: failed to open log file %s\n", qPrintable(path));
    }

    qInstallMessageHandler(messageHandler);
}
