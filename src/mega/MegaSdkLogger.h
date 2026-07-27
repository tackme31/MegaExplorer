#pragma once
#include <megaapi.h> // derives from mega::MegaLogger, a complete-type base class

// Bridges mega::MegaLogger's callback into this app's categorized Qt logging
// (src/app/Logging.h's lcSdk category), so SDK-internal diagnostics (network,
// transfers, cache) land in the same log file as our own qCWarning calls
// instead of going nowhere. Registered/unregistered via
// MegaApi::addLoggerObject/removeLoggerObject in MegaSdkClient's constructor/
// destructor. Lives in src/mega, not src/app, because deriving from
// mega::MegaLogger requires including megaapi.h -- same rule as
// MegaSdkClient itself (see MegaSdkClient.h).
class MegaSdkLogger : public mega::MegaLogger
{
public:
    void log(const char* time, int logLevel, const char* source, const char* message) override;
};
