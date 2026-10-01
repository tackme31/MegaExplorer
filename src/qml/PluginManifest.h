#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

struct PluginCommand
{
    QString id;
    QString title;
};

// One plugin's plugin.json (docs/investigations/STUDY_PLUGIN_V1_DESIGN.md §4).
struct PluginManifest
{
    QString id;
    QString name;
    QString version;
    // The folder plugin.json was read from: the working directory of the process,
    // and what a relative run.command is resolved against.
    QString dir;
    QString command;
    QStringList args;
    std::vector<PluginCommand> commands;
};

// Unknown keys are ignored. On failure returns nullopt and, when error is given,
// a one-line reason for the log.
std::optional<PluginManifest>
parsePluginManifest(const QByteArray& json, const QString& dir, QString* error = nullptr);

// run.command as an absolute path, or "" when it cannot be found. A command with
// a path separator is relative to the plugin's folder; a bare name is searched on
// PATH, as CreateProcess would.
QString resolvePluginProgram(const PluginManifest& manifest);
