#include "AppIdentity.h"

#include <QCoreApplication>
#include <QRegularExpression>

void applyAppIdentity()
{
    QCoreApplication::setOrganizationName(QStringLiteral("MegaExplorer"));

    QString name = QStringLiteral("MegaExplorer");
    // The suffix lands in a filesystem path and a registry key, so anything but
    // word characters is dropped rather than escaped at each use site.
    const QString profile =
        qEnvironmentVariable("MEGAEXPLORER_PROFILE").remove(QRegularExpression("[^A-Za-z0-9_-]"));
    if (!profile.isEmpty())
        name += "-" + profile;
    QCoreApplication::setApplicationName(name);
}
