#include "MegaSdkLogger.h"

#include "app/Logging.h"

void MegaSdkLogger::log(const char* time, int logLevel, const char* source, const char* message)
{
    // FATAL/ERROR/WARNING collapse onto qCWarning: an SDK-internal FATAL
    // message aborting this app via qCritical/qFatal would be far more
    // surprising than losing that severity distinction.
    switch (logLevel)
    {
        case mega::MegaApi::LOG_LEVEL_FATAL:
        case mega::MegaApi::LOG_LEVEL_ERROR:
        case mega::MegaApi::LOG_LEVEL_WARNING:
            qCWarning(lcSdk) << time << source << message;
            break;
        case mega::MegaApi::LOG_LEVEL_INFO:
            qCInfo(lcSdk) << time << source << message;
            break;
        default: // DEBUG, VERBOSE
            qCDebug(lcSdk) << time << source << message;
            break;
    }
}
