#include "qml/PluginManifest.h"

#include <gtest/gtest.h>

namespace
{
const QByteArray kValid = R"({
  "id": "com.example.hello",
  "name": "Hello",
  "version": "0.1.0",
  "apiVersion": 1,
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
    EXPECT_EQ(manifest->apiVersion, 1);
}

TEST(PluginManifestTest, ReadsDescriptionAndRepositoryUrl)
{
    const auto manifest = parsePluginManifest(R"({
      "id": "x", "name": "X", "apiVersion": 1, "run": { "command": "x" },
      "description": "Tags images.", "repositoryUrl": "https://github.com/example/x",
      "commands": [ { "id": "a", "title": "A" } ]
    })", QStringLiteral("C:/p"));
    ASSERT_TRUE(manifest.has_value());
    EXPECT_EQ(manifest->description, QStringLiteral("Tags images."));
    EXPECT_EQ(manifest->repositoryUrl, QStringLiteral("https://github.com/example/x"));

    const auto bare = parsePluginManifest(kValid, QStringLiteral("C:/p"));
    ASSERT_TRUE(bare.has_value());
    EXPECT_TRUE(bare->description.isEmpty());
    EXPECT_TRUE(bare->repositoryUrl.isEmpty());
}

TEST(PluginManifestTest, DropsARepositoryUrlThatIsNotHttp)
{
    for (const char* url : {"file:///C:/Windows/system32/calc.exe", "javascript:alert(1)",
                            "github.com/example/x", "https://", "ftp://example.com/x"})
    {
        const QByteArray json = QByteArray(R"({
          "id": "x", "name": "X", "apiVersion": 1, "run": { "command": "x" },
          "repositoryUrl": ")") + url + R"(", "commands": [ { "id": "a", "title": "A" } ]
        })";
        const auto manifest = parsePluginManifest(json, QStringLiteral("C:/p"));
        ASSERT_TRUE(manifest.has_value()) << url;
        EXPECT_TRUE(manifest->repositoryUrl.isEmpty()) << url;
    }
}

TEST(PluginManifestTest, ReadsPermissionsWithoutDuplicates)
{
    const auto manifest = parsePluginManifest(R"({
      "id": "x", "name": "X", "apiVersion": 1, "run": { "command": "x" },
      "permissions": ["items.read", "content.read", "items.read"],
      "commands": [ { "id": "a", "title": "A" } ]
    })", QStringLiteral("C:/p"));
    ASSERT_TRUE(manifest.has_value());
    EXPECT_EQ(manifest->permissions, (QStringList{QStringLiteral("items.read"), QStringLiteral("content.read")}));

    const auto bare = parsePluginManifest(kValid, QStringLiteral("C:/p"));
    ASSERT_TRUE(bare.has_value());
    EXPECT_TRUE(bare->permissions.isEmpty());
}

TEST(PluginManifestTest, RejectsAnUnknownOrMalformedPermission)
{
    for (const char* permissions : {R"(["items.read", "items.trash"])", R"(["network"])", R"("items.read")",
                                    R"([1])", R"([""])"})
    {
        const QByteArray json = QByteArray(R"({
          "id": "x", "name": "X", "apiVersion": 1, "run": { "command": "x" },
          "permissions": )") + permissions + R"(, "commands": [ { "id": "a", "title": "A" } ]
        })";
        EXPECT_TRUE(errorFor(json).contains(QStringLiteral("permission"))) << permissions;
    }
}

TEST(PluginManifestTest, KeepsAnApiVersionThisAppDoesNotSpeak)
{
    QByteArray json = kValid;
    json.replace("\"apiVersion\": 1", "\"apiVersion\": 7");
    const auto manifest = parsePluginManifest(json, QStringLiteral("C:/p"));
    ASSERT_TRUE(manifest.has_value());
    EXPECT_EQ(manifest->apiVersion, 7);
}

TEST(PluginManifestTest, RejectsAMissingOrBadApiVersion)
{
    for (const char* replacement : {"", "\"apiVersion\": \"1\",", "\"apiVersion\": 0,", "\"apiVersion\": 1.5,"})
    {
        QByteArray json = kValid;
        json.replace("\"apiVersion\": 1,", replacement);
        EXPECT_TRUE(errorFor(json).contains(QStringLiteral("apiVersion"))) << replacement;
    }
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

TEST(PluginManifestTest, ReadsWhereTheResultGoes)
{
    QByteArray json = kValid;
    json.replace(R"("title": "B" })", R"("title": "B", "result": "dialog" })");
    const auto manifest = parsePluginManifest(json, QStringLiteral("C:/p"));
    ASSERT_TRUE(manifest.has_value());
    EXPECT_FALSE(manifest->commands[0].resultInDialog);
    EXPECT_TRUE(manifest->commands[1].resultInDialog);

    QByteArray bad = kValid;
    bad.replace(R"("title": "B" })", R"("title": "B", "result": "window" })");
    EXPECT_TRUE(errorFor(bad).contains(QStringLiteral("result")));
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

TEST(PluginManifestTest, ReadsWhichProgressFiguresToShowInDisplayOrder)
{
    const auto manifest = parsePluginManifest(R"({
      "id": "x", "name": "X", "apiVersion": 1, "run": { "command": "x" },
      "commands": [
        { "id": "plain", "title": "P", "progress": true },
        { "id": "none", "title": "N", "progress": { "show": [] } },
        { "id": "bare", "title": "B", "progress": {} },
        { "id": "some", "title": "S", "progress": { "show": ["remaining", "count", "elapsed"] } },
        { "id": "off", "title": "O" },
        { "id": "never", "title": "V", "progress": "never" }
      ]
    })", QStringLiteral("C:/p"));
    ASSERT_TRUE(manifest.has_value());
    const std::vector<PluginCommand>& commands = manifest->commands;
    EXPECT_TRUE(commands[0].progress);
    EXPECT_TRUE(commands[0].progressShow.isEmpty());
    EXPECT_TRUE(commands[1].progress);
    EXPECT_TRUE(commands[1].progressShow.isEmpty());
    EXPECT_TRUE(commands[2].progress);
    EXPECT_TRUE(commands[3].progress);
    EXPECT_EQ(commands[3].progressShow,
              (QStringList{QStringLiteral("count"), QStringLiteral("elapsed"), QStringLiteral("remaining")}));
    EXPECT_FALSE(commands[4].progress);
    EXPECT_FALSE(commands[4].progressNever);
    EXPECT_FALSE(commands[5].progress);
    EXPECT_TRUE(commands[5].progressNever);
}

TEST(PluginManifestTest, RejectsABadProgress)
{
    for (const char* progress : {R"("yes")", R"({"show": "count"})", R"({"show": ["count", "eta"]})", "1", R"("Never")"})
    {
        const QByteArray json = QByteArray(R"({"id": "x", "name": "X", "apiVersion": 1, "run": {"command": "x"},
            "commands": [{"id": "a", "title": "A", "progress": )") + progress + "}]}";
        EXPECT_TRUE(errorFor(json).contains(QStringLiteral("\"progress\""))) << progress;
    }
}
