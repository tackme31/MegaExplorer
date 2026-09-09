#pragma once

// Sets QCoreApplication's organization/application name, which is what every
// per-user storage location in this app resolves from: AppLocalDataLocation
// (session token, SDK state cache, log file) and QSettings' registry key.
//
// MEGAEXPLORER_PROFILE suffixes the application name, so a build run with
// MEGAEXPLORER_PROFILE=dev keeps a separate MEGA login from the everyday one.
// Unset means the production names, unchanged.
//
// Must run before installLogging() and before the first QSettings use.
void applyAppIdentity();
