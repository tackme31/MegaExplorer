#pragma once

// Sets QCoreApplication's organization/application name, which is what every
// per-user storage location in this app resolves from: AppLocalDataLocation
// (session token, SDK state cache, log file) and QSettings' registry key.
//
// A profile name suffixes the application name, so a build using one keeps a
// separate MEGA login, settings and log from the everyday one. It comes from
// MEGAEXPLORER_PROFILE, or, when that is unset, from MEGAEXPLORER_DEFAULT_PROFILE,
// which CMake sets to "dev" for Debug builds -- launching the exe directly must
// not silently share the released build's account. No profile at all means the
// production names, unchanged.
//
// Must run before installLogging() and before the first QSettings use.
void applyAppIdentity();
