#pragma once
#include <QLoggingCategory>

// One category per functional area, so a log line's origin is filterable via
// QT_LOGGING_RULES without touching code. lcSdk carries mega::MegaLogger's
// bridged output (see src/mega/MegaSdkLogger.h).
Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcNavigation)
Q_DECLARE_LOGGING_CATEGORY(lcSearch)
Q_DECLARE_LOGGING_CATEGORY(lcDownload)
Q_DECLARE_LOGGING_CATEGORY(lcUpload)
Q_DECLARE_LOGGING_CATEGORY(lcThumbnail)
Q_DECLARE_LOGGING_CATEGORY(lcSdk)
Q_DECLARE_LOGGING_CATEGORY(lcSession)
Q_DECLARE_LOGGING_CATEGORY(lcAuth)
Q_DECLARE_LOGGING_CATEGORY(lcQuickAccess)
Q_DECLARE_LOGGING_CATEGORY(lcFileOps)

// Installs a message handler that writes every qCWarning/qCInfo/etc. call to
// both stderr (so Qt Creator's Application Output still works) and a log
// file under AppDataLocation. Must run before any other logging call in the
// process -- in particular before main.cpp's env-var check -- since
// appMegaExplorer is WIN32_EXECUTABLE, so qWarning() otherwise reaches no
// visible destination at all on a normal launch. Depends on
// QCoreApplication::setOrganizationName/setApplicationName having already
// run (AppDataLocation resolves through them).
void installLogging();
