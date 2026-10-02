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

// Fills command's targets/extensions from "when"; returns the reason on a bad value.
std::optional<QString> readWhen(const QJsonObject& commandObj, PluginCommand* command)
{
    const QJsonValue whenValue = commandObj.value(QStringLiteral("when"));
    if (whenValue.isUndefined())
        return std::nullopt;
    if (!whenValue.isObject())
        return QStringLiteral("\"when\" must be an object");
    const QJsonObject when = whenValue.toObject();

    const QJsonValue targets = when.value(QStringLiteral("targets"));
    if (targets == QJsonValue(QStringLiteral("files")))
        command->targets = PluginTargets::Files;
    else if (targets == QJsonValue(QStringLiteral("folders")))
        command->targets = PluginTargets::Folders;
    else if (!targets.isUndefined() && targets != QJsonValue(QStringLiteral("any")))
        return QStringLiteral("\"when.targets\" must be \"files\", \"folders\" or \"any\"");

    const QJsonValue extensions = when.value(QStringLiteral("extensions"));
    if (extensions.isUndefined())
        return std::nullopt;
    if (!extensions.isArray())
        return QStringLiteral("\"when.extensions\" must be a list of strings");
    for (const QJsonValue extension : extensions.toArray())
    {
        QString text = extension.toString();
        if (text.startsWith(QLatin1Char('.')))
            text.remove(0, 1);
        if (!extension.isString() || text.isEmpty())
            return QStringLiteral("\"when.extensions\" must be a list of non-empty strings");
        command->extensions << text.toLower();
    }
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
    const QJsonValue apiVersion = root.value(QStringLiteral("apiVersion"));
    if (!apiVersion.isDouble() || apiVersion.toDouble() != apiVersion.toInt() || apiVersion.toInt() < 1)
        return fail(error, QStringLiteral("\"apiVersion\" is missing or not a positive integer"));
    manifest.apiVersion = apiVersion.toInt();

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
                              obj.value(QStringLiteral("title")).toString(),
                              obj.value(QStringLiteral("progress")).toBool()};
        if (command.id.isEmpty() || command.id.contains(QLatin1Char('/')) ||
            command.title.isEmpty())
            return fail(error,
                        QStringLiteral("a command needs an \"id\" without '/' and a \"title\""));
        const QJsonValue result = obj.value(QStringLiteral("result"));
        if (!result.isUndefined() && result != QStringLiteral("toast") && result != QStringLiteral("dialog"))
            return fail(error,
                        QStringLiteral("command \"%1\": \"result\" must be \"toast\" or \"dialog\"")
                            .arg(command.id));
        command.resultInDialog = result == QStringLiteral("dialog");
        if (const std::optional<QString> whenError = readWhen(obj, &command))
            return fail(error, QStringLiteral("command \"%1\": %2").arg(command.id, *whenError));
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

bool pluginCommandAccepts(const PluginCommand& command,
                          const std::vector<PluginSelectionItem>& selection)
{
    if (selection.empty())
        return false;
    for (const PluginSelectionItem& item : selection)
    {
        if (command.targets == PluginTargets::Files && item.isFolder)
            return false;
        if (command.targets == PluginTargets::Folders && !item.isFolder)
            return false;
        if (command.extensions.isEmpty())
            continue;
        const qsizetype dot = item.name.lastIndexOf(QLatin1Char('.'));
        if (item.isFolder || dot < 0 ||
            !command.extensions.contains(item.name.mid(dot + 1).toLower()))
            return false;
    }
    return true;
}
