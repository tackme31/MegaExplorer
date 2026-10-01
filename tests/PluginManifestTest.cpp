#include "qml/PluginManifest.h"

#include <gtest/gtest.h>

namespace
{
const QByteArray kValid = R"({
  "id": "com.example.hello",
  "name": "Hello",
  "version": "0.1.0",
  "unknownKey": true,
  "run": { "command": "uv", "args": ["run", "main.py"] },
  "commands": [ { "id": "a", "title": "A" }, { "id": "b", "title": "B" } ]
})";

QString errorFor(const QByteArray& json)
{
    QString error;
    EXPECT_FALSE(parsePluginManifest(json, QStringLiteral("C:/p"), &error).has_value());
    return error;
}
} // namespace

TEST(PluginManifestTest, ParsesAValidManifest)
{
    const auto manifest = parsePluginManifest(kValid, QStringLiteral("C:/p"));
    ASSERT_TRUE(manifest.has_value());
    EXPECT_EQ(manifest->id, QStringLiteral("com.example.hello"));
    EXPECT_EQ(manifest->name, QStringLiteral("Hello"));
    EXPECT_EQ(manifest->dir, QStringLiteral("C:/p"));
    EXPECT_EQ(manifest->command, QStringLiteral("uv"));
    EXPECT_EQ(manifest->args, (QStringList{QStringLiteral("run"), QStringLiteral("main.py")}));
    ASSERT_EQ(manifest->commands.size(), 2u);
    EXPECT_EQ(manifest->commands[1].title, QStringLiteral("B"));
}

TEST(PluginManifestTest, ReadsTheProgressFlagPerCommand)
{
    QByteArray json = kValid;
    json.replace(R"("title": "B" })", R"("title": "B", "progress": true })");
    const auto manifest = parsePluginManifest(json, QStringLiteral("C:/p"));
    ASSERT_TRUE(manifest.has_value());
    EXPECT_FALSE(manifest->commands[0].progress);
    EXPECT_TRUE(manifest->commands[1].progress);
}

TEST(PluginManifestTest, RejectsBrokenJson)
{
    EXPECT_TRUE(errorFor("{").contains(QStringLiteral("JSON")));
}

TEST(PluginManifestTest, RejectsAnIdOutsideTheAlphabet)
{
    QByteArray json = kValid;
    json.replace("com.example.hello", "Com/Example");
    EXPECT_TRUE(errorFor(json).contains(QStringLiteral("id")));
}

TEST(PluginManifestTest, RejectsAMissingRunCommand)
{
    QByteArray json = kValid;
    json.replace("\"command\": \"uv\",", "");
    EXPECT_TRUE(errorFor(json).contains(QStringLiteral("run.command")));
}

TEST(PluginManifestTest, RejectsDuplicateCommandIds)
{
    QByteArray json = kValid;
    json.replace("\"id\": \"b\"", "\"id\": \"a\"");
    EXPECT_TRUE(errorFor(json).contains(QStringLiteral("twice")));
}

TEST(PluginManifestTest, RejectsACommandIdWithASlash)
{
    QByteArray json = kValid;
    json.replace("\"id\": \"b\"", "\"id\": \"b/c\"");
    EXPECT_FALSE(errorFor(json).isEmpty());
}

TEST(PluginManifestTest, RejectsNoCommands)
{
    QByteArray json = kValid;
    const qsizetype start = json.indexOf("\"commands\"");
    json.truncate(start);
    json.append("\"commands\": [] }");
    EXPECT_TRUE(errorFor(json).contains(QStringLiteral("commands")));
}

TEST(PluginManifestTest, ResolvesARelativeCommandAgainstThePluginFolder)
{
    PluginManifest manifest;
    manifest.dir = QStringLiteral("C:/definitely/not/here");
    manifest.command = QStringLiteral("bin/plugin.exe");
    EXPECT_TRUE(resolvePluginProgram(manifest).isEmpty());
}

TEST(PluginManifestTest, DefaultsToAnyTargetWithoutWhen)
{
    const auto manifest = parsePluginManifest(kValid, QStringLiteral("C:/p"));
    ASSERT_TRUE(manifest.has_value());
    EXPECT_EQ(manifest->commands[0].targets, PluginTargets::Any);
    EXPECT_TRUE(manifest->commands[0].extensions.isEmpty());
}

TEST(PluginManifestTest, ReadsWhenTargetsAndNormalisesExtensions)
{
    QByteArray json = kValid;
    json.replace(R"("title": "A" })",
                 R"("title": "A", "when": { "targets": "files", "extensions": [".PNG", "jpg"] } })");
    json.replace(R"("title": "B" })", R"("title": "B", "when": { "targets": "folders" } })");
    const auto manifest = parsePluginManifest(json, QStringLiteral("C:/p"));
    ASSERT_TRUE(manifest.has_value());
    EXPECT_EQ(manifest->commands[0].targets, PluginTargets::Files);
    EXPECT_EQ(manifest->commands[0].extensions,
              (QStringList{QStringLiteral("png"), QStringLiteral("jpg")}));
    EXPECT_EQ(manifest->commands[1].targets, PluginTargets::Folders);
}

TEST(PluginManifestTest, RejectsABadWhen)
{
    for (const char* when : {R"("when": "files")",
                             R"("when": { "targets": "images" })",
                             R"("when": { "extensions": "png" })",
                             R"("when": { "extensions": ["."] })",
                             R"("when": { "extensions": [1] })"})
    {
        QByteArray json = kValid;
        json.replace(R"("title": "A" })", QByteArray(R"("title": "A", )") + when + " }");
        EXPECT_TRUE(errorFor(json).contains(QStringLiteral("command \"a\""))) << when;
    }
}

TEST(PluginManifestTest, AcceptsBySelectionTargetsAndExtensions)
{
    const PluginSelectionItem image{QStringLiteral("a.PNG"), false};
    const PluginSelectionItem pdf{QStringLiteral("b.pdf"), false};
    const PluginSelectionItem bare{QStringLiteral("README"), false};
    const PluginSelectionItem folder{QStringLiteral("photos.png"), true};

    PluginCommand any;
    EXPECT_TRUE(pluginCommandAccepts(any, {image, folder}));
    EXPECT_FALSE(pluginCommandAccepts(any, {}));

    PluginCommand files;
    files.targets = PluginTargets::Files;
    EXPECT_TRUE(pluginCommandAccepts(files, {image, pdf}));
    EXPECT_FALSE(pluginCommandAccepts(files, {image, folder}));

    PluginCommand folders;
    folders.targets = PluginTargets::Folders;
    EXPECT_TRUE(pluginCommandAccepts(folders, {folder}));
    EXPECT_FALSE(pluginCommandAccepts(folders, {folder, image}));

    PluginCommand images;
    images.extensions = {QStringLiteral("png")};
    EXPECT_TRUE(pluginCommandAccepts(images, {image}));
    EXPECT_FALSE(pluginCommandAccepts(images, {image, pdf}));
    EXPECT_FALSE(pluginCommandAccepts(images, {bare}));
    EXPECT_FALSE(pluginCommandAccepts(images, {folder}));
}
