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
    // Exact lists, in menu order -- this is the contract FileContextMenu.qml's
    // Instantiator renders, so ordering matters as much as membership. Rename
    // and toggleFavourite drop out of the multi case (ActionArity::SingleOnly).
    FileListModel modelSingle;
    modelSingle.setEntries(makeEntries(3));
    modelSingle.selectRow(0, 0);
    EXPECT_EQ(modelSingle.availableActions(),
              (QStringList{"download",
                           "openLocalLocation",
                           "toggleFavourite",
                           "copyLink",
                           "removeLink",
                           "cut",
                           "copy",
                           "rename",
                           "moveToRubbish"}));

    FileListModel modelMulti;
    modelMulti.setEntries(makeEntries(3));
    modelMulti.selectRow(0, 0);
    modelMulti.selectRow(1, kCtrl);
    EXPECT_EQ(modelMulti.availableActions(),
              (QStringList{"download", "cut", "copy", "moveToRubbish"}));
}

TEST(FileListModelTest, AvailableActionsOffersOnlyMoveToRubbishForAMixedSelection)
{
    FileListModel model;
    model.setEntries(std::vector<FileEntry>{makeEntry("a", 0), makeFolderEntry("b", 1)});
    model.selectRow(0, 0);
    model.selectRow(1, kCtrl);

    // Download is FilesOnly and Rename is SingleOnly, so the clipboard pair and
    // deleting are all that a mixed multi-selection can do.
    EXPECT_EQ(model.availableActions(), (QStringList{"cut", "copy", "moveToRubbish"}));
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

// -- Rubber-band selection (Phase 21) ----------------------------------------

TEST(FileListModelTest, BandReplacesSelectionWhenNotAdditive)
{
    FileListModel model;
    model.setEntries(makeEntries(6));
    model.selectRow(5, 0);

    model.beginBandSelection(false);
    model.updateBandSelection(1, 3);
    model.endBandSelection();

    EXPECT_EQ(selectedHandlesSorted(model), (std::vector<quint64>{1, 2, 3}));
}

TEST(FileListModelTest, AdditiveBandKeepsSelectionFromBeforeTheDrag)
{
    FileListModel model;
    model.setEntries(makeEntries(6));
    model.selectRow(5, 0);

    model.beginBandSelection(true);
    model.updateBandSelection(1, 2);
    model.endBandSelection();

    EXPECT_EQ(selectedHandlesSorted(model), (std::vector<quint64>{1, 2, 5}));
}

TEST(FileListModelTest, ShrinkingBandDeselectsAgainButKeepsTheBase)
{
    FileListModel model;
    model.setEntries(makeEntries(6));
    model.selectRow(5, 0);

    model.beginBandSelection(true);
    model.updateBandSelection(0, 4);
    model.updateBandSelection(0, 1);

    EXPECT_EQ(selectedHandlesSorted(model), (std::vector<quint64>{0, 1, 5}));
}

TEST(FileListModelTest, EmptyBandLeavesNothingSelected)
{
    FileListModel model;
    model.setEntries(makeEntries(4));
    model.selectRow(2, 0);

    model.beginBandSelection(false);
    model.updateBandSelection(-1, -1);
    model.endBandSelection();

    EXPECT_TRUE(selectedHandlesSorted(model).empty());
    EXPECT_EQ(model.cursorRow(), -1);
}

TEST(FileListModelTest, GridBandSelectsOnlyTheCoveredBlock)
{
    // 9 rows over 3 columns: rows 1-2 of the grid, columns 0-1 -> model rows
    // 3, 4, 6, 7.
    FileListModel model;
    model.setEntries(makeEntries(9));

    model.beginBandSelection(false);
    model.updateBandSelectionGrid(1, 2, 3, 0, 1);
    model.endBandSelection();

    EXPECT_EQ(selectedHandlesSorted(model), (std::vector<quint64>{3, 4, 6, 7}));
}

TEST(FileListModelTest, GridBandClampsPastTheLastPartialRow)
{
    // 5 entries over 3 columns: the last grid row holds rows 3 and 4 only, and
    // a band dragged well past the end must not address anything beyond them.
    FileListModel model;
    model.setEntries(makeEntries(5));

    model.beginBandSelection(false);
    model.updateBandSelectionGrid(1, 99, 3, 0, 2);
    model.endBandSelection();

    EXPECT_EQ(selectedHandlesSorted(model), (std::vector<quint64>{3, 4}));
}

TEST(FileListModelTest, BandLeavesAnchorAndCursorOnItsFirstAndLastRow)
{
    FileListModel model;
    model.setEntries(makeEntries(8));

    model.beginBandSelection(false);
    model.updateBandSelection(2, 4);
    model.endBandSelection();

    EXPECT_EQ(model.cursorRow(), 4);

    // Shift+click extends from the band's first row, not its last.
    model.selectRow(6, kShift);
    EXPECT_EQ(selectedHandlesSorted(model), (std::vector<quint64>{2, 3, 4, 5, 6}));
}

TEST(FileListModelTest, CancelledBandRestoresTheSelectionItStartedFrom)
{
    FileListModel model;
    model.setEntries(makeEntries(6));
    model.selectRow(5, 0);

    model.beginBandSelection(true);
    model.updateBandSelection(0, 3);
    model.cancelBandSelection();

    EXPECT_EQ(selectedHandlesSorted(model), (std::vector<quint64>{5}));
}

TEST(FileListModelTest, BandUpdatesAfterTheSessionEndedAreIgnored)
{
    FileListModel model;
    model.setEntries(makeEntries(4));

    model.beginBandSelection(false);
    model.updateBandSelection(0, 1);
    model.endBandSelection();
    model.updateBandSelection(2, 3);

    EXPECT_EQ(selectedHandlesSorted(model), (std::vector<quint64>{0, 1}));
}

TEST(FileListModelTest, SetEntriesDropsTheBandSession)
{
    FileListModel model;
    model.setEntries(makeEntries(4));

    model.beginBandSelection(false);
    model.updateBandSelection(0, 1);
    model.setEntries(makeEntries(4)); // same handles: the selection survives
    model.updateBandSelection(2, 3);  // ... but this addresses a dead session

    EXPECT_EQ(selectedHandlesSorted(model), (std::vector<quint64>{0, 1}));
}

TEST(FileListModelTest, BandRepaintsOnlyTheRowsThatChanged)
{
    FileListModel model;
    model.setEntries(makeEntries(100));

    int firstChanged = -1;
    int lastChanged = -1;
    QObject::connect(&model,
                     &FileListModel::dataChanged,
                     [&](const QModelIndex& topLeft, const QModelIndex& bottomRight, auto) {
                         firstChanged = topLeft.row();
                         lastChanged = bottomRight.row();
                     });

    model.beginBandSelection(false);
    model.updateBandSelection(10, 12);

    EXPECT_EQ(firstChanged, 10);
    EXPECT_EQ(lastChanged, 12);
}

TEST(FileListModelTest, SelectedEntryIsEmptyUnlessExactlyOneRowIsSelected)
{
    FileListModel model;
    model.setEntries(makeEntries(5));

    EXPECT_TRUE(model.selectedEntry().isEmpty());

    model.selectRow(1, Qt::NoModifier);
    model.selectRow(3, Qt::ControlModifier);
    EXPECT_TRUE(model.selectedEntry().isEmpty());
}

TEST(FileListModelTest, SelectedEntryCarriesSizeBytesUnlikeEntryAt)
{
    FileListModel model;
    // The preview pane gates text fetches on this size, so it has to be present.
    model.setEntries({FileEntry{"notes.txt", 42, 1234, false, 0, false}});

    model.selectRow(0, Qt::NoModifier);

    const QVariantMap entry = model.selectedEntry();
    EXPECT_EQ(entry.value("handle").toULongLong(), 42u);
    EXPECT_EQ(entry.value("name").toString(), QStringLiteral("notes.txt"));
    EXPECT_EQ(entry.value("sizeBytes").toULongLong(), 1234u);
    EXPECT_FALSE(entry.value("isFolder").toBool());
    EXPECT_FALSE(model.entryAt(0).contains("sizeBytes"));
}

TEST(FileListModelTest, IsFavouriteRoleReportsTheEntryFlag)
{
    FileListModel model;
    model.setEntries({FileEntry{"a", 1, 0, false, 0, false, true},
                      FileEntry{"b", 2, 0, false, 0, false, false}});

    EXPECT_TRUE(model.data(model.index(0, 0), FileListModel::IsFavouriteRole).toBool());
    EXPECT_FALSE(model.data(model.index(1, 0), FileListModel::IsFavouriteRole).toBool());
}

TEST(FileListModelTest, SetFavouriteUpdatesOneRowWithoutResettingTheModel)
{
    FileListModel model;
    model.setEntries(makeEntries(5));

    int resets = 0;
    int changedRow = -1;
    QVector<int> changedRoles;
    QObject::connect(&model, &FileListModel::modelReset, [&]() {
        ++resets;
    });
    QObject::connect(
        &model,
        &FileListModel::dataChanged,
        [&](const QModelIndex& topLeft, const QModelIndex&, const QVector<int>& roles) {
            changedRow = topLeft.row();
            changedRoles = roles;
        });

    model.setFavourite(3, true);

    // A reset here would relayout the grid and drop the scroll position, which
    // is the whole reason this isn't routed through setEntries().
    EXPECT_EQ(resets, 0);
    EXPECT_EQ(changedRow, 3);
    EXPECT_EQ(changedRoles, QVector<int>{FileListModel::IsFavouriteRole});
    EXPECT_TRUE(model.data(model.index(3, 0), FileListModel::IsFavouriteRole).toBool());
}

TEST(FileListModelTest, SetFavouriteIgnoresAHandleThatIsNoLongerListed)
{
    FileListModel model;
    model.setEntries(makeEntries(3));

    int changes = 0;
    QObject::connect(&model, &FileListModel::dataChanged, [&]() {
        ++changes;
    });

    model.setFavourite(999, true);

    EXPECT_EQ(changes, 0);
}

TEST(FileListModelTest, SelectedEntryCarriesIsFavouriteForTheContextMenu)
{
    FileListModel model;
    // FileContextMenu.qml reads this key to pick between "Add to Favourites"
    // and "Remove from Favourites".
    model.setEntries({FileEntry{"pinned.txt", 7, 0, false, 0, false, true}});

    model.selectRow(0, Qt::NoModifier);

    EXPECT_TRUE(model.selectedEntry().value("isFavourite").toBool());
    EXPECT_TRUE(model.selectedEntries().first().toMap().value("isFavourite").toBool());
}

TEST(FileListModelTest, AvailableActionsDropTheMovingOnesInAFavouritesListing)
{
    // Same selection, two screens: the view kind is the only difference, and it
    // takes cut and moveToRubbish away (FAVOURITES_VIEW_SPEC.md 4.1).
    FileListModel model;
    model.setEntries(makeEntries(3));
    model.selectRow(0, Qt::NoModifier);
    ASSERT_EQ(model.availableActions(),
              (QStringList{"download",
                           "openLocalLocation",
                           "toggleFavourite",
                           "copyLink",
                           "removeLink",
                           "cut",
                           "copy",
                           "rename",
                           "moveToRubbish"}));

    model.setViewKind(ViewKind::Favourites);

    EXPECT_EQ(model.availableActions(),
              (QStringList{"download",
                           "openLocalLocation",
                           "toggleFavourite",
                           "copyLink",
                           "removeLink",
                           "copy",
                           "rename"}));
}

TEST(FileListModelTest, SetViewKindNotifiesOnlyOnAChange)
{
    // availableActions is a Q_PROPERTY whose NOTIFY is selectionChanged, and the
    // kind is one of its inputs -- so a real change has to emit, and a no-op mustn't.
    FileListModel model;
    int emitted = 0;
    QObject::connect(&model, &FileListModel::selectionChanged, [&emitted]() {
        ++emitted;
    });

    model.setViewKind(ViewKind::CloudDrive);
    EXPECT_EQ(emitted, 0);

    model.setViewKind(ViewKind::Favourites);
    EXPECT_EQ(emitted, 1);
}
