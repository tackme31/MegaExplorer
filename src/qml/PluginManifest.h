#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

enum class PluginTargets
{
    Any,
    Files,
    Folders
};

struct PluginCommand
{
    QString id;
    QString title;
    // Shows the progress dialog while the command runs.
    bool progress = false;
    // "result": "dialog": the run's message ends up in a dialog (the progress
    // dialog, when there is one) instead of a toast.
    bool resultInDialog = false;
    // when.targets / when.extensions: a selection that fails them greys the row.
    PluginTargets targets = PluginTargets::Any;
    // Lower case, without the dot. Empty accepts every extension.
    QStringList extensions;
};

struct PluginSelectionItem
{
    QString name;
    bool isFolder = false;
};

// Whether the command's row is enabled for this selection. Every item must pass;
// a folder never passes a non-empty extensions list.
bool pluginCommandAccepts(const PluginCommand& command,
                          const std::vector<PluginSelectionItem>& selection);;

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
