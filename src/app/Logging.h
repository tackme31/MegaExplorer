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
Q_DECLARE_LOGGING_CATEGORY(lcAccount)

// Installs a message handler writing every log call to both the console streams and
// a file under AppLocalDataLocation. Must run before any other logging call: this is
// a WIN32_EXECUTABLE, so output otherwise reaches no visible destination at all.
// Depends on QCoreApplication::setOrganizationName/setApplicationName having run.
void installLogging();
