#include "AppIdentity.h"

#include <QCoreApplication>
#include <QRegularExpression>

// Set per build configuration in CMakeLists.txt; a target that does not set it
// gets the production names.
#ifndef MEGAEXPLORER_DEFAULT_PROFILE
#define MEGAEXPLORER_DEFAULT_PROFILE ""
#endif

void applyAppIdentity()
{
    QCoreApplication::setOrganizationName(QStringLiteral("MegaExplorer"));

    QString name = QStringLiteral("MegaExplorer");
    // Empty rather than unset is not a distinguishable override on Windows --
    // clearing an environment variable there removes it -- so the compiled-in
    // default is what an unset variable falls back to.
    QString profile = qEnvironmentVariable("MEGAEXPLORER_PROFILE");
    if (profile.isEmpty())
        profile = QStringLiteral(MEGAEXPLORER_DEFAULT_PROFILE);
    // The suffix lands in a filesystem path and a registry key, so anything but
    // word characters is dropped rather than escaped at each use site.
    profile.remove(QRegularExpression("[^A-Za-z0-9_-]"));
    if (!profile.isEmpty())
        name += "-" + profile;
    QCoreApplication::setApplicationName(name);
}
