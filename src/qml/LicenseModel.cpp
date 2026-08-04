#include "LicenseModel.h"

#include "app/Logging.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
// qt_add_qml_module derives the resource prefix from the URI, so licenses/ as
// listed in RESOURCES lands here.
constexpr auto kQrcBaseDir = ":/qt/qml/MegaExplorer/licenses";
} // namespace

LicenseModel::LicenseModel(QObject* parent) : LicenseModel(QString::fromLatin1(kQrcBaseDir), parent)
{}

LicenseModel::LicenseModel(const QString& baseDir, QObject* parent)
    : QAbstractListModel(parent), m_baseDir(baseDir)
{
    load();
}

void LicenseModel::load()
{
    const QString manifestPath = m_baseDir + QStringLiteral("/manifest.json");
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        // An empty dialog is a visible bug and a compliance problem, but it is
        // recoverable; refusing to start would be worse.
        qCWarning(lcApp) << "license manifest not readable:" << manifestPath;
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        qCWarning(lcApp) << "license manifest is not valid JSON:" << parseError.errorString();
        return;
    }

    const QJsonArray components = document.object().value(QStringLiteral("components")).toArray();
    m_components.reserve(components.size());
    for (const QJsonValue& value : components)
    {
        const QJsonObject object = value.toObject();
        m_components.append(Component{
            object.value(QStringLiteral("name")).toString(),
            object.value(QStringLiteral("version")).toString(),
            object.value(QStringLiteral("license")).toString(),
            object.value(QStringLiteral("homepage")).toString(),
            object.value(QStringLiteral("text")).toString(),
        });
    }
}

int LicenseModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_components.size());
}

QVariant LicenseModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_components.size())
        return {};

    const Component& component = m_components.at(index.row());
    switch (role)
    {
        case NameRole:
            return component.name;
        case VersionRole:
            return component.version;
        case LicenseRole:
            return component.license;
        case HomepageRole:
            return component.homepage;
        default:
            return {};
    }
}

QHash<int, QByteArray> LicenseModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {VersionRole, "version"},
        {LicenseRole, "license"},
        {HomepageRole, "homepage"},
    };
}

QString LicenseModel::licenseText(int row) const
{
    if (row < 0 || row >= m_components.size())
        return {};

    const auto cached = m_textCache.constFind(row);
    if (cached != m_textCache.constEnd())
        return *cached;

    const QString path = m_baseDir + QLatin1Char('/') + m_components.at(row).textPath;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        qCWarning(lcApp) << "license text not readable:" << path;
        return {};
    }
    // Not cached on failure, so a transient read error can still recover.
    return *m_textCache.insert(row, QString::fromUtf8(file.readAll()));
}
