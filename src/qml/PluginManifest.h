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
    // "progress": {"show": [...]}: the figures under the dialog's bar, in the order of
    // kPluginProgressFigures whatever order they were listed in. Empty shows none.
    QStringList progressShow;
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
// The plugin protocol version this app speaks. Bump it only on a breaking change:
// a plugin declaring any other version is listed but cannot run.
inline constexpr int kPluginApiVersion = 1;

// Every name "progress.show" may hold, in display order; anything else rejects the manifest.
inline const QStringList kPluginProgressFigures{
    QStringLiteral("count"), QStringLiteral("rate"), QStringLiteral("elapsed"), QStringLiteral("remaining")};

// Every name "permissions" may hold; anything else rejects the manifest.
bool isKnownPluginPermission(const QString& permission);

struct PluginManifest
{
    QString id;
    QString name;
    QString version;
    QString description;
    // Only an http(s) URL is kept, since it will be opened as a link; anything else is "".
    QString repositoryUrl;
    int apiVersion = 0;
    // The folder plugin.json was read from: the working directory of the process,
    // and what a relative run.command is resolved against.
    QString dir;
    QString command;
    QStringList args;
    // Declared host-API permissions, without duplicates. Missing means none.
    QStringList permissions;
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
