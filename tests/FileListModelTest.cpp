#include "qml/FileListModel.h"

#include <QVariantMap>

#include <algorithm>
#include <gtest/gtest.h>
#include <vector>

namespace
{

FileEntry makeEntry(const std::string& name, std::uint64_t handle)
{
    // Aggregate init: {name, handle, sizeBytes, isFolder, modificationTime,
    // hasThumbnail} -- only name/handle matter for these selection/cursor
    // tests.
    return FileEntry{name, handle, 0, false, 0, false};
}

FileEntry makeFolderEntry(const std::string& name, std::uint64_t handle)
{
    return FileEntry{name, handle, 0, true, 0, false};
}

std::vector<FileEntry> makeEntries(int count)
{
    std::vector<FileEntry> entries;
    entries.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        entries.push_back(makeEntry("entry" + std::to_string(i), static_cast<std::uint64_t>(i)));
    return entries;
}

// makeEntries() above gives handle == row index, so tests can assert on
// handles directly without a separate row-lookup step.
std::vector<quint64> selectedHandlesSorted(const FileListModel& model)
{
    const QVariantList variants = model.selectedHandlesVariant();
    std::vector<quint64> handles;
    handles.reserve(static_cast<std::size_t>(variants.size()));
    for (const QVariant& v : variants)
        handles.push_back(v.toULongLong());
    std::sort(handles.begin(), handles.end());
    return handles;
}

constexpr int kShift = static_cast<int>(Qt::ShiftModifier);
constexpr int kCtrl = static_cast<int>(Qt::ControlModifier);

} // namespace

TEST(FileListModelTest, NewModelHasNoCursorAndMoveCursorIsNoOp)
{
    FileListModel model;

    int signalCount = 0;
    QObject::connect(&model, &FileListModel::selectionChanged, [&signalCount]() {
        ++signalCount;
    });

    EXPECT_EQ(model.cursorRow(), -1);
    model.moveCursor(1, 0);

    EXPECT_EQ(model.cursorRow(), -1);
    EXPECT_EQ(signalCount, 0);
}

TEST(FileListModelTest, MoveCursorWithNoSelectionIsNoOp)
{
    FileListModel model;
    model.setEntries(makeEntries(5));

    int signalCount = 0;
    QObject::connect(&model, &FileListModel::selectionChanged, [&signalCount]() {
        ++signalCount;
    });

    model.moveCursor(1, 0);

    EXPECT_EQ(model.cursorRow(), -1);
    EXPECT_EQ(signalCount, 0);
}

TEST(FileListModelTest, PlainArrowMovesSelectionByOne)
{
    FileListModel model;
    model.setEntries(makeEntries(5));

    model.selectRow(2, 0);
    model.moveCursor(1, 0);

    EXPECT_EQ(model.cursorRow(), 3);
    EXPECT_EQ(selectedHandlesSorted(model), (std::vector<quint64>{3}));
}

TEST(FileListModelTest, ArrowDoesNotWrapAtEdges)
{
    FileListModel model;
    model.setEntries(makeEntries(5));

    model.selectRow(0, 0);
    model.moveCursor(-1, 0);
    EXPECT_EQ(model.cursorRow(), 0);

    model.selectRow(4, 0);
    model.moveCursor(1, 0);
    EXPECT_EQ(model.cursorRow(), 4);
}

TEST(FileListModelTest, MoveCursorClampsPastEnd)
{
    FileListModel model;
    model.setEntries(makeEntries(10));

    model.selectRow(8, 0);
    model.moveCursor(5, 0);

    EXPECT_EQ(model.cursorRow(), 9);
}

TEST(FileListModelTest, CtrlModifierBehavesLikePlainArrow)
{
    FileListModel modelPlain;
    modelPlain.setEntries(makeEntries(5));
    modelPlain.selectRow(2, 0);
    modelPlain.moveCursor(1, 0);

    FileListModel modelCtrl;
    modelCtrl.setEntries(makeEntries(5));
    modelCtrl.selectRow(2, 0);
    modelCtrl.moveCursor(1, kCtrl);

    EXPECT_EQ(modelCtrl.cursorRow(), modelPlain.cursorRow());
    EXPECT_EQ(selectedHandlesSorted(modelCtrl), selectedHandlesSorted(modelPlain));
}

TEST(FileListModelTest, ShiftArrowExtendsAndShrinksFromAnchor)
{
    FileListModel model;
    model.setEntries(makeEntries(6));

    model.selectRow(2, 0);
    model.moveCursor(1, kShift);
    model.moveCursor(1, kShift);
    EXPECT_EQ(selectedHandlesSorted(model), (std::vector<quint64>{2, 3, 4}));

    model.moveCursor(-1, kShift);
    EXPECT_EQ(selectedHandlesSorted(model), (std::vector<quint64>{2, 3}));
}

TEST(FileListModelTest, MoveCursorShiftUsesAnchorNotPriorCursor)
{
    FileListModel model;
    model.setEntries(makeEntries(8));

    model.selectRow(2, 0);       // anchor = cursor = 2
    model.selectRow(5, kShift);  // anchor stays 2, cursor -> 5, selection {2..5}
    model.moveCursor(1, kShift); // range must still start at anchor 2, not cursor 5

    EXPECT_EQ(selectedHandlesSorted(model), (std::vector<quint64>{2, 3, 4, 5, 6}));
    EXPECT_EQ(model.cursorRow(), 6);
}

TEST(FileListModelTest, PlainArrowAtEdgeCollapsesMultiSelection)
{
    FileListModel model;
    model.setEntries(makeEntries(5));

    model.selectRow(2, 0);
    model.selectRow(4, kShift); // {2,3,4}, cursor at last row (4)
    model.moveCursor(1, 0);     // target clamps to 4 == from -- must still collapse

    EXPECT_EQ(selectedHandlesSorted(model), (std::vector<quint64>{4}));
    EXPECT_EQ(model.cursorRow(), 4);
}

TEST(FileListModelTest, SelectAllSelectsEveryRowAndKeepsValidCursor)
{
    FileListModel model;
    model.setEntries(makeEntries(5));
    model.selectRow(3, 0); // anchor = cursor = 3

    model.selectAll();

    EXPECT_EQ(selectedHandlesSorted(model), (std::vector<quint64>{0, 1, 2, 3, 4}));
    EXPECT_EQ(model.cursorRow(), 3); // kept, not moved to row 0
}

TEST(FileListModelTest, SelectAllSeedsCursorFromRowZeroWhenUnset)
{
    FileListModel model;
    model.setEntries(makeEntries(4));

    model.selectAll();

    EXPECT_EQ(model.cursorRow(), 0);
}

TEST(FileListModelTest, SelectAllOnEmptyModelIsNoOp)
{
    FileListModel model;

    int signalCount = 0;
    QObject::connect(&model, &FileListModel::selectionChanged, [&signalCount]() {
        ++signalCount;
    });

    model.selectAll();

    EXPECT_EQ(model.cursorRow(), -1);
    EXPECT_EQ(signalCount, 0);
}

TEST(FileListModelTest, CursorSurvivesResortOfSameHandles)
{
    FileListModel model;
    model.setEntries(makeEntries(4)); // handles 0..3 at rows 0..3
    model.selectRow(1, 0);            // cursor handle = 1, currently at row 1

    std::vector<FileEntry> reordered{
        makeEntry("d", 3), makeEntry("c", 2), makeEntry("b", 1), makeEntry("a", 0)};
    model.setEntries(std::move(reordered)); // handle 1 now at row 2

    EXPECT_EQ(model.cursorRow(), 2);
}

TEST(FileListModelTest, CursorIsPrunedOnNavigationToDifferentHandles)
{
    FileListModel model;
    model.setEntries(makeEntries(4)); // handles 0..3
    model.selectRow(1, 0);

    std::vector<FileEntry> unrelated{makeEntry("x", 100), makeEntry("y", 101)};
    model.setEntries(std::move(unrelated)); // no shared handles

    EXPECT_EQ(model.cursorRow(), -1);
}

TEST(FileListModelTest, SelectionSummaryCountsFilesAndFolders)
{
    FileListModel model;
    model.setEntries(
        std::vector<FileEntry>{makeEntry("a", 0), makeEntry("b", 1), makeFolderEntry("c", 2)});
    model.selectRow(0, 0);
    model.selectRow(1, kCtrl);
    model.selectRow(2, kCtrl);

    SelectionSummary summary = model.selectionSummary();
    EXPECT_EQ(summary.fileCount, 2);
    EXPECT_EQ(summary.folderCount, 1);
}

TEST(FileListModelTest, AvailableActionsIsEmptyWithNoSelection)
{
    FileListModel model;
    model.setEntries(makeEntries(3));

    EXPECT_TRUE(model.availableActions().isEmpty());
}

TEST(FileListModelTest, AvailableActionsOffersDownloadForFileSelection)
{
    FileListModel modelSingle;
    modelSingle.setEntries(makeEntries(3));
    modelSingle.selectRow(0, 0);
    EXPECT_EQ(modelSingle.availableActions(), (QStringList{"download"}));

    FileListModel modelMulti;
    modelMulti.setEntries(makeEntries(3));
    modelMulti.selectRow(0, 0);
    modelMulti.selectRow(1, kCtrl);
    EXPECT_EQ(modelMulti.availableActions(), (QStringList{"download"}));
}

TEST(FileListModelTest, AvailableActionsIsEmptyWhenSelectionContainsAFolder)
{
    FileListModel model;
    model.setEntries(std::vector<FileEntry>{makeEntry("a", 0), makeFolderEntry("b", 1)});
    model.selectRow(0, 0);
    model.selectRow(1, kCtrl);

    EXPECT_TRUE(model.availableActions().isEmpty());
}

TEST(FileListModelTest, AvailableActionsClearedAfterNavigation)
{
    FileListModel model;
    model.setEntries(makeEntries(3));
    model.selectRow(0, 0);
    ASSERT_FALSE(model.availableActions().isEmpty());

    model.setEntries(std::vector<FileEntry>{makeEntry("x", 100), makeEntry("y", 101)});

    EXPECT_TRUE(model.availableActions().isEmpty());
}

TEST(FileListModelTest, SelectedEntriesAreReturnedInRowOrder)
{
    FileListModel model;
    model.setEntries(makeEntries(5)); // handles 0..4, row == handle

    // Select via Ctrl-click in reverse row order so insertion order into the
    // underlying unordered_set doesn't happen to match row order --
    // selectedEntries() must still come back in row order regardless.
    model.selectRow(4, 0);
    model.selectRow(2, kCtrl);
    model.selectRow(0, kCtrl);

    QVariantList entries = model.selectedEntries();
    ASSERT_EQ(entries.size(), 3);
    EXPECT_EQ(entries[0].toMap()["handle"].toULongLong(), 0u);
    EXPECT_EQ(entries[1].toMap()["handle"].toULongLong(), 2u);
    EXPECT_EQ(entries[2].toMap()["handle"].toULongLong(), 4u);
}

TEST(FileListModelTest, SelectedEntriesCarryNameSizeAndIsFolder)
{
    FileListModel model;
    model.setEntries(std::vector<FileEntry>{
        FileEntry{"file.txt", 1, 12345, false, 0, false},
        FileEntry{"folder", 2, 0, true, 0, false},
    });
    model.selectRow(0, 0);
    model.selectRow(1, kCtrl);

    QVariantList selected = model.selectedEntries();
    ASSERT_EQ(selected.size(), 2);

    QVariantMap file = selected[0].toMap();
    EXPECT_EQ(file["name"].toString(), QStringLiteral("file.txt"));
    EXPECT_EQ(file["sizeBytes"].toULongLong(), 12345u);
    EXPECT_FALSE(file["isFolder"].toBool());

    QVariantMap folder = selected[1].toMap();
    EXPECT_EQ(folder["name"].toString(), QStringLiteral("folder"));
    EXPECT_TRUE(folder["isFolder"].toBool());
}

TEST(FileListModelTest, SelectedEntriesIsEmptyWithNoSelection)
{
    FileListModel model;
    model.setEntries(makeEntries(3));

    EXPECT_TRUE(model.selectedEntries().isEmpty());
}
