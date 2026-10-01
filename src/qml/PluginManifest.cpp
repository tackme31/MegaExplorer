#include "PluginManifest.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

namespace
{
// Also a folder name and a settings key later, hence the narrow alphabet.
bool isValidPluginId(const QString& id)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9][a-z0-9.-]*$"));
    return pattern.match(id).hasMatch();
}

std::optional<PluginManifest> fail(QString* error, const QString& reason)
{
    if (error)
        *error = reason;
    return std::nullopt;
}
} // namespace

std::optional<PluginManifest>
parsePluginManifest(const QByteArray& json, const QString& dir, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(error, QStringLiteral("not valid JSON: %1").arg(parseError.errorString()));
    if (!doc.isObject())
        return fail(error, QStringLiteral("top level is not an object"));
    const QJsonObject root = doc.object();

    PluginManifest manifest;
    manifest.dir = dir;
    manifest.id = root.value(QStringLiteral("id")).toString();
    if (!isValidPluginId(manifest.id))
        return fail(error, QStringLiteral("\"id\" is missing or not [a-z0-9.-]"));
    manifest.name = root.value(QStringLiteral("name")).toString();
    if (manifest.name.isEmpty())
        return fail(error, QStringLiteral("\"name\" is missing"));
    manifest.version = root.value(QStringLiteral("version")).toString();

    const QJsonObject run = root.value(QStringLiteral("run")).toObject();
    manifest.command = run.value(QStringLiteral("command")).toString();
    if (manifest.command.isEmpty())
        return fail(error, QStringLiteral("\"run.command\" is missing"));
    for (const QJsonValue arg : run.value(QStringLiteral("args")).toArray())
    {
        if (!arg.isString())
            return fail(error, QStringLiteral("\"run.args\" must be strings"));
        manifest.args << arg.toString();
    }

    for (const QJsonValue value : root.value(QStringLiteral("commands")).toArray())
    {
        const QJsonObject obj = value.toObject();
        PluginCommand command{obj.value(QStringLiteral("id")).toString(),
                              obj.value(QStringLiteral("title")).toString()};
        if (command.id.isEmpty() || command.id.contains(QLatin1Char('/')) ||
            command.title.isEmpty())
            return fail(error,
                        QStringLiteral("a command needs an \"id\" without '/' and a \"title\""));
        for (const PluginCommand& existing : manifest.commands)
        {
            if (existing.id == command.id)
                return fail(error,
                            QStringLiteral("command id \"%1\" appears twice").arg(command.id));
        }
        manifest.commands.push_back(std::move(command));
    }
    if (manifest.commands.empty())
        return fail(error, QStringLiteral("\"commands\" is missing or empty"));
    return manifest;
}

QString resolvePluginProgram(const PluginManifest& manifest)
{
    const QString& command = manifest.command;
    if (command.contains(QLatin1Char('/')) || command.contains(QLatin1Char('\\')))
    {
        const QFileInfo info(QDir(manifest.dir), command);
        return info.isFile() ? info.absoluteFilePath() : QString();
    }
    return QStandardPaths::findExecutable(command);
}
