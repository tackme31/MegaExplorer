#include "Logging.h"

#include <QDir>
#include <QFile>
#include <QMutex>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>
#include <cstdlib>

#include <windows.h>

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
Q_LOGGING_CATEGORY(lcSession, "megaexplorer.session")
Q_LOGGING_CATEGORY(lcAuth, "megaexplorer.auth")
Q_LOGGING_CATEGORY(lcQuickAccess, "megaexplorer.quickaccess")
Q_LOGGING_CATEGORY(lcFileOps, "megaexplorer.fileops")

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

// Qt Creator's Application Output pane paints the whole stderr stream red
// regardless of content, so routing every level there made info/debug look
// identical to errors. The stream split alone only buys two tiers (stdout
// normal / stderr red), so warnings additionally carry an ANSI color code.
// Downside of the split: the two streams reach a consumer as separate pipes
// and can interleave out of order under bursts -- the log file below is
// written from one sink and stays the authoritative chronological record.
FILE* streamFor(QtMsgType type)
{
    return (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) ? stderr : stdout;
}

// Deliberately not gated on _isatty: the main consumer is Qt Creator's
// Application Output, which decodes SGR codes but is a pipe, not a tty, so
// an isatty check suppressed exactly the case this exists for. Cost is
// escape sequences appearing verbatim if console output is redirected to a
// file -- the log file sink is the supported way to capture a run anyway.
const char* colorCodeFor(QtMsgType type)
{
    switch (type)
    {
    case QtWarningMsg:
        return "\x1b[33m"; // yellow
    case QtCriticalMsg:
    case QtFatalMsg:
        return "\x1b[31m"; // red -- for real terminals; Qt Creator already reds stderr
    default:
        return nullptr; // QtDebugMsg / QtInfoMsg: stdout's default color, no codes needed
    }
}
constexpr const char* kAnsiReset = "\x1b[0m";

// Legacy conhost.exe needs ENABLE_VIRTUAL_TERMINAL_PROCESSING opted in per
// handle before it'll render SGR color codes instead of printing them as
// garbage; Windows Terminal supports it unconditionally, but this is
// harmless there too. GetConsoleMode fails (and is skipped) when the handle
// isn't an actual console -- e.g. Qt Creator's Application Output, which
// captures our stdout/stderr through a pipe -- so this only ever affects
// real terminals.
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

    // qInstallMessageHandler's contract requires the handler to be
    // thread-safe: MegaSdkLogger forwards MEGA SDK callbacks that can fire
    // from an SDK-internal background thread, in addition to GUI-thread
    // qCWarning calls from controllers.
    QMutexLocker lock(&logMutex());
    FILE* const stream = streamFor(type);
    if (const char* const color = colorCodeFor(type))
        fprintf(stream, "%s%s%s\n", color, qPrintable(formatted), kAnsiReset);
    else
        fprintf(stream, "%s\n", qPrintable(formatted));
    // stdout is block-buffered whenever it isn't a console (Qt Creator, shell
    // redirect): without this, info/debug lines sit in the CRT buffer and only
    // surface when the process exits. MSVC's _IOLBF is an alias for full
    // buffering, so per-line flushing is the only option short of _IONBF.
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
