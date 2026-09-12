#include "qml/OpenWithController.h"

#include <QCoreApplication>
#include <QDir>
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QString>
#include <QUrl>
#include <QVariantMap>

#include <filesystem>
#include <gtest/gtest.h>
#include <vector>

namespace
{

// One unique temp INI per test, for QSettingsPinnedFolderStoreTest's reason: the
// QSettings cache is keyed by file path, so a shared path leaks in-memory state
// between tests.
QString tempSettingsPath(const std::string& testName)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                       ("megaexplorer_openwith_test_" + testName + ".ini");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return QString::fromStdString(path.string());
}

// Deliberately not QSignalSpy: that lives in Qt6::Test, which this target does
// not link (see UploadControllerTest's note on the same choice). Both signals
// here are emitted synchronously, so a plain connect is enough.
struct LaunchResult
{
    bool ok = false;
    QString name;
};

QVariantMap makeEntry(const char* name, const char* extensions, const char* commandLine)
{
    QVariantMap map;
    map.insert(QStringLiteral("name"), QString::fromLatin1(name));
    map.insert(QStringLiteral("extensions"), QString::fromLatin1(extensions));
    map.insert(QStringLiteral("commandLine"), QString::fromLatin1(commandLine));
    return map;
}

} // namespace

// nullptr for the ViewerController throughout: every test here is about the saved
// list, and launch() is the only member that reaches for one -- covered by its own
// case below, which asserts the null is refused rather than dereferenced.

TEST(OpenWithControllerTest, StartsEmptyWithNothingStored)
{
    OpenWithController controller(nullptr, tempSettingsPath("empty"));
    EXPECT_EQ(controller.count(), 0);
    EXPECT_TRUE(controller.entries().isEmpty());
}

TEST(OpenWithControllerTest, SavedEntriesSurviveAnotherInstance)
{
    const QString path = tempSettingsPath("roundtrip");
    {
        OpenWithController controller(nullptr, path);
        controller.setEntries({makeEntry("Viewer", "jpg, png", "\"C:\\Apps\\v.exe\" --source %U"),
                               makeEntry("Player", "", "vlc.exe")});
        ASSERT_EQ(controller.count(), 2);
    }

    OpenWithController reopened(nullptr, path);
    ASSERT_EQ(reopened.count(), 2);
    const QVariantList entries = reopened.entries();
    // Read back exactly as typed: the settings list shows these strings to the
    // user, so no normalization may happen on the way through.
    EXPECT_EQ(entries[0].toMap().value(QStringLiteral("name")).toString(),
              QStringLiteral("Viewer"));
    EXPECT_EQ(entries[0].toMap().value(QStringLiteral("extensions")).toString(),
              QStringLiteral("jpg, png"));
    EXPECT_EQ(entries[0].toMap().value(QStringLiteral("commandLine")).toString(),
              QStringLiteral("\"C:\\Apps\\v.exe\" --source %U"));
    EXPECT_EQ(reopened.nameAt(1), QStringLiteral("Player"));
}

TEST(OpenWithControllerTest, RejectsEntriesWithNoNameOrNoCommand)
{
    OpenWithController controller(nullptr, tempSettingsPath("incomplete"));
    controller.setEntries({makeEntry("", "jpg", "v.exe"),
                           makeEntry("Half typed", "jpg", ""),
                           makeEntry("Keeper", "jpg", "v.exe")});
    ASSERT_EQ(controller.count(), 1);
    EXPECT_EQ(controller.nameAt(0), QStringLiteral("Keeper"));
}

TEST(OpenWithControllerTest, EntriesChangedOnlyFiresOnARealChange)
{
    OpenWithController controller(nullptr, tempSettingsPath("signal"));
    int changes = 0;
    QObject::connect(&controller, &OpenWithController::entriesChanged, [&changes]() {
        ++changes;
    });

    controller.setEntries({makeEntry("Viewer", "jpg", "v.exe")});
    EXPECT_EQ(changes, 1);
    controller.setEntries({makeEntry("Viewer", "jpg", "v.exe")});
    EXPECT_EQ(changes, 1);
    controller.setEntries({});
    EXPECT_EQ(changes, 2);
}

TEST(OpenWithControllerTest, MatchesAtFollowsTheEntrysExtensions)
{
    OpenWithController controller(nullptr, tempSettingsPath("matches"));
    controller.setEntries(
        {makeEntry("Viewer", "jpg, png", "v.exe"), makeEntry("Anything", "", "a.exe")});

    EXPECT_TRUE(controller.matchesAt(0, QStringLiteral("photo.JPG")));
    EXPECT_FALSE(controller.matchesAt(0, QStringLiteral("clip.mp4")));
    EXPECT_TRUE(controller.matchesAt(1, QStringLiteral("clip.mp4")));
    // Out of range answers "not offered" rather than asserting: the IDs come
    // from QML, which can be one menu behind an edit in the settings screen.
    EXPECT_FALSE(controller.matchesAt(5, QStringLiteral("photo.jpg")));
    EXPECT_FALSE(controller.matchesAt(-1, QStringLiteral("photo.jpg")));
    EXPECT_TRUE(controller.nameAt(5).isEmpty());
}

TEST(OpenWithControllerTest, LaunchReportsFailureForAnIndexThatIsNotThere)
{
    OpenWithController controller(nullptr, tempSettingsPath("launch"));
    std::vector<LaunchResult> results;
    QObject::connect(&controller,
                     &OpenWithController::programLaunched,
                     [&results](bool ok, const QString& name) {
                         results.push_back({ok, name});
                     });

    controller.launch(0, 42);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].ok);
}

TEST(OpenWithControllerTest, LaunchWithoutAViewerStartsNothing)
{
    OpenWithController controller(nullptr, tempSettingsPath("noviewer"));
    controller.setEntries({makeEntry("Viewer", "", "\"C:\\nonexistent\\v.exe\" %U")});
    std::vector<LaunchResult> results;
    QObject::connect(&controller,
                     &OpenWithController::programLaunched,
                     [&results](bool ok, const QString& name) {
                         results.push_back({ok, name});
                     });

    controller.launch(0, 42);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].ok);
    EXPECT_EQ(results[0].name, QStringLiteral("Viewer"));
}

TEST(OpenWithControllerTest, StoredJunkLeavesTheListEmpty)
{
    const QString path = tempSettingsPath("junk");
    {
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue(QStringLiteral("openWith/programs"), QStringLiteral("not json"));
        settings.sync();
    }

    OpenWithController controller(nullptr, path);
    EXPECT_EQ(controller.count(), 0);
}

TEST(OpenWithControllerTest, CommandRunnableResolvesTheProgramToken)
{
    OpenWithController controller(nullptr, tempSettingsPath("runnable"));
    const QString self = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());

    EXPECT_TRUE(controller.commandRunnable(QLatin1Char('"') + self + QStringLiteral("\" %U")));
    EXPECT_FALSE(controller.commandRunnable(QStringLiteral("\"C:\\nonexistent\\v.exe\" %U")));
    EXPECT_FALSE(controller.commandRunnable(QStringLiteral("megaexplorer-no-such-program %U")));
    EXPECT_FALSE(controller.commandRunnable(QStringLiteral("   ")));
}

TEST(OpenWithControllerTest, CommandRunnableAcceptsAPathWrittenWithoutExe)
{
    OpenWithController controller(nullptr, tempSettingsPath("noexe"));
    QString self = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    ASSERT_TRUE(self.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive));
    self.chop(4);

    EXPECT_TRUE(controller.commandRunnable(QLatin1Char('"') + self + QLatin1Char('"')));
}

TEST(OpenWithControllerTest, CommandWithProgramTakesTheChoosersUrl)
{
    OpenWithController controller(nullptr, tempSettingsPath("withprogram"));
    const QUrl picked = QUrl::fromLocalFile(QStringLiteral("C:/Apps/Viewer/v.exe"));

    const QString quoted = QStringLiteral("\"C:\\Apps\\Viewer\\v.exe\"");

    EXPECT_EQ(controller.commandWithProgram(QString(), picked), quoted + QStringLiteral(" %U"));
    EXPECT_EQ(controller.commandWithProgram(QStringLiteral("   "), picked),
              quoted + QStringLiteral(" %U"));
    EXPECT_EQ(controller.commandWithProgram(QStringLiteral("old.exe  --source   %U"), picked),
              quoted + QStringLiteral("  --source   %U"));
    EXPECT_EQ(controller.commandWithProgram(QStringLiteral("\"C:\\Old Dir\\a.exe\" %U"), picked),
              quoted + QStringLiteral(" %U"));
}

// The token has to end where splitCommand() ends it, or the arguments after a
// separator it recognises would be swallowed into the replaced program.
TEST(OpenWithControllerTest, CommandWithProgramEndsTheTokenWhereSplitCommandDoes)
{
    OpenWithController controller(nullptr, tempSettingsPath("tokenend"));
    const QUrl picked = QUrl::fromLocalFile(QStringLiteral("C:/v.exe"));
    const QString quoted = QStringLiteral("\"C:\\v.exe\"");

    // Quoting part-way through a token does not end it.
    EXPECT_EQ(
        controller.commandWithProgram(QStringLiteral("C:\\\"Program Files\"\\x.exe %U"), picked),
        quoted + QStringLiteral(" %U"));
    // A full-width space (U+3000) separates, as QChar::isSpace() says.
    const QString fullWidth =
        QStringLiteral("vlc.exe") + QChar(0x3000) + QStringLiteral("--fullscreen %U");
    EXPECT_EQ(controller.commandWithProgram(fullWidth, picked),
              quoted + QChar(0x3000) + QStringLiteral("--fullscreen %U"));
    // Three quotes are a literal quote and leave quoting off.
    EXPECT_EQ(controller.commandWithProgram(QStringLiteral("a\"\"\"b c"), picked),
              quoted + QStringLiteral(" c"));
    for (const QString& command : {fullWidth, QStringLiteral("a\"\"\"b c")})
        EXPECT_EQ(QProcess::splitCommand(controller.commandWithProgram(command, picked)).size(),
                  QProcess::splitCommand(command).size());
}
