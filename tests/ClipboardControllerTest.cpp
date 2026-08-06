#include "qml/ClipboardController.h"

#include "TestApp.h"

#include <QVariantMap>

#include <gtest/gtest.h>

namespace
{

QVariantList entries(std::initializer_list<std::pair<quint64, const char*>> items,
                     bool isFolder = false)
{
    QVariantList list;
    for (const auto& item : items)
    {
        QVariantMap map;
        map[QStringLiteral("handle")] = item.first;
        map[QStringLiteral("name")] = QString::fromLatin1(item.second);
        map[QStringLiteral("isFolder")] = isFolder;
        list.append(map);
    }
    return list;
}

class ClipboardControllerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        testApp();
        QObject::connect(&clipboard, &ClipboardController::contentChanged, &clipboard, [this]() {
            ++changes;
        });
    }

    ClipboardController clipboard;
    int changes = 0;
};

} // namespace

TEST_F(ClipboardControllerTest, IsEmptyByDefault)
{
    EXPECT_FALSE(clipboard.hasContent());
    EXPECT_FALSE(clipboard.isCut());
    EXPECT_EQ(clipboard.count(), 0);
    EXPECT_TRUE(clipboard.cutHandles().isEmpty());
}

TEST_F(ClipboardControllerTest, CopyStoresTheEntriesAndReportsNotCut)
{
    clipboard.copy(entries({{1, "a"}, {2, "b"}}), 7, false);

    EXPECT_TRUE(clipboard.hasContent());
    EXPECT_FALSE(clipboard.isCut());
    EXPECT_EQ(clipboard.count(), 2);
    ASSERT_EQ(clipboard.entries().size(), 2u);
    EXPECT_EQ(clipboard.entries()[0].handle, 1u);
    EXPECT_EQ(clipboard.entries()[1].name, QStringLiteral("b"));
    EXPECT_EQ(clipboard.sourceHandle(), 7u);
    EXPECT_FALSE(clipboard.sourceIsRoot());
    EXPECT_EQ(changes, 1);
}

TEST_F(ClipboardControllerTest, CopyLeavesCutHandlesEmpty)
{
    clipboard.copy(entries({{1, "a"}}), 7, false);
    EXPECT_TRUE(clipboard.cutHandles().isEmpty());
}

TEST_F(ClipboardControllerTest, CutReportsCutAndListsItsHandles)
{
    clipboard.cut(entries({{1, "a"}, {2, "b"}}), 7, false);

    EXPECT_TRUE(clipboard.isCut());
    const QVariantList handles = clipboard.cutHandles();
    ASSERT_EQ(handles.size(), 2);
    EXPECT_EQ(handles[0].toULongLong(), 1u);
    EXPECT_EQ(handles[1].toULongLong(), 2u);
}

TEST_F(ClipboardControllerTest, CopyAfterACutNotifiesSoTheGhostingClears)
{
    clipboard.cut(entries({{1, "a"}}), 7, false);
    clipboard.copy(entries({{1, "a"}}), 7, false);

    EXPECT_FALSE(clipboard.isCut());
    EXPECT_TRUE(clipboard.cutHandles().isEmpty());
    EXPECT_EQ(changes, 2);
}

TEST_F(ClipboardControllerTest, ClearEmptiesEverythingAndNotifies)
{
    clipboard.cut(entries({{1, "a"}}), 7, false);
    clipboard.clear();

    EXPECT_FALSE(clipboard.hasContent());
    EXPECT_FALSE(clipboard.isCut());
    EXPECT_EQ(changes, 2);
}

TEST_F(ClipboardControllerTest, ClearOnAnEmptyClipboardDoesNotNotify)
{
    clipboard.clear();
    EXPECT_EQ(changes, 0);
}

TEST_F(ClipboardControllerTest, CanPasteIntoRejectsAnEmptyClipboard)
{
    EXPECT_FALSE(clipboard.canPasteInto(7, false));
    EXPECT_FALSE(clipboard.canPasteInto(0, true));
}

TEST_F(ClipboardControllerTest, CanPasteIntoRejectsACutBackIntoItsSourceFolder)
{
    clipboard.cut(entries({{1, "a"}}), 7, false);

    EXPECT_FALSE(clipboard.canPasteInto(7, false));
    EXPECT_TRUE(clipboard.canPasteInto(8, false));
    EXPECT_TRUE(clipboard.canPasteInto(0, true));
}

TEST_F(ClipboardControllerTest, CanPasteIntoRejectsACutBackIntoTheRoot)
{
    clipboard.cut(entries({{1, "a"}}), 0, true);

    EXPECT_FALSE(clipboard.canPasteInto(0, true));
    // Handle 0 with isRoot false is a different folder, not the root -- the
    // sentinel convention this whole codebase carries around.
    EXPECT_TRUE(clipboard.canPasteInto(0, false));
}

TEST_F(ClipboardControllerTest, CanPasteIntoAcceptsACopyBackIntoItsSourceFolder)
{
    // The auto-rename case, and the main reason paste isn't simply greyed out
    // whenever the destination is the source.
    clipboard.copy(entries({{1, "a"}}), 7, false);
    EXPECT_TRUE(clipboard.canPasteInto(7, false));
}
