#include "QSettingsPinnedFolderStore.h"

#include "app/Logging.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QString>

namespace
{
const char* const kFieldName = "name";
const char* const kFieldHandle = "handle";

// accountKey is a decimal MEGA user handle (IPinnedFolderStore's doc
// comment) -- digits only, so no escaping is needed for a QSettings key path.
QString settingsKeyFor(const std::string& accountKey)
{
    return QStringLiteral("quickAccess/accounts/%1/pinnedFolders")
        .arg(QString::fromStdString(accountKey));
}
} // namespace

QSettingsPinnedFolderStore::QSettingsPinnedFolderStore() = default;
QSettingsPinnedFolderStore::~QSettingsPinnedFolderStore() = default;

Result<std::vector<PinnedFolder>>
QSettingsPinnedFolderStore::load(const std::string& accountKey) const
{
    QSettings settings;
    const QString stored = settings.value(settingsKeyFor(accountKey)).toString();
    if (stored.isEmpty())
        return Result<std::vector<PinnedFolder>>::ok({});

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(stored.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray())
    {
        qCWarning(lcQuickAccess) << "stored quick-access pins are not a valid JSON array:"
                                 << parseError.errorString();
        return Result<std::vector<PinnedFolder>>::fail("stored quick-access pins are corrupt");
    }

    std::vector<PinnedFolder> pins;
    const QJsonArray array = document.array();
    pins.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValue& value : array)
    {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        // A handle of 0 is never a real node, so it can only come from a
        // truncated/hand-edited entry -- skip rather than fail the whole load.
        const qulonglong handle =
            static_cast<qulonglong>(object.value(QString::fromLatin1(kFieldHandle)).toDouble());
        if (handle == 0)
            continue;
        PinnedFolder pin;
        pin.name = object.value(QString::fromLatin1(kFieldName)).toString().toStdString();
        pin.handle = static_cast<std::uint64_t>(handle);
        pins.push_back(std::move(pin));
    }

    return Result<std::vector<PinnedFolder>>::ok(std::move(pins));
}

Result<void> QSettingsPinnedFolderStore::save(const std::string& accountKey,
                                              const std::vector<PinnedFolder>& pins)
{
    QJsonArray array;
    for (const PinnedFolder& pin : pins)
    {
        QJsonObject object;
        object.insert(QString::fromLatin1(kFieldName), QString::fromStdString(pin.name));
        // double, not the (absent) qint64 overload: QJsonValue has no
        // unsigned 64-bit type, and a MEGA handle is 48 bits in practice, so
        // it round-trips through a double exactly.
        object.insert(QString::fromLatin1(kFieldHandle), static_cast<double>(pin.handle));
        array.append(object);
    }

    QSettings settings;
    settings.setValue(settingsKeyFor(accountKey),
                      QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    settings.sync();
    if (settings.status() != QSettings::NoError)
    {
        qCWarning(lcQuickAccess) << "failed to write quick-access pins, status="
                                 << settings.status();
        return Result<void>::fail("failed to save quick-access pins");
    }

    return Result<void>::ok();
}
