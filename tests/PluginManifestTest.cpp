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
