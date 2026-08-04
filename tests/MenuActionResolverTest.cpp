#include "core/MenuActionResolver.h"

#include <gtest/gtest.h>

namespace
{

// The site every target/arity case below is about: FileSelection is the only
// site whose target actually varies, which is why those two axes exist at all.
MenuContext fileSelection(int files, int folders)
{
    MenuContext ctx;
    ctx.site = MenuSite::FileSelection;
    ctx.selection.fileCount = files;
    ctx.selection.folderCount = folders;
    return ctx;
}

MenuActionSpec spec(ActionTarget target, ActionArity arity)
{
    // The action value itself never matters to menuActionApplies -- only
    // site/target/arity are inspected -- so every synthetic spec here reuses
    // one real MenuAction value.
    return MenuActionSpec{MenuAction::Download, {MenuSite::FileSelection}, target, arity};
}

const ActionTarget kAllTargets[] = {
    ActionTarget::Any, ActionTarget::FilesOnly, ActionTarget::FoldersOnly};
const ActionArity kAllArities[] = {
    ActionArity::Any, ActionArity::SingleOnly, ActionArity::MultiOnly};

bool contains(const std::vector<MenuAction>& actions, MenuAction action)
{
    for (MenuAction a : actions)
    {
        if (a == action)
            return true;
    }
    return false;
}

} // namespace

TEST(MenuActionResolverTest, EverySpecRejectsEmptySelection)
{
    for (ActionTarget target : kAllTargets)
    {
        for (ActionArity arity : kAllArities)
        {
            EXPECT_FALSE(menuActionApplies(spec(target, arity), fileSelection(0, 0)))
                << "target=" << static_cast<int>(target) << " arity=" << static_cast<int>(arity);
        }
    }
}

TEST(MenuActionResolverTest, AnyTargetAcceptsFilesFoldersAndMixed)
{
    MenuActionSpec s = spec(ActionTarget::Any, ActionArity::Any);
    EXPECT_TRUE(menuActionApplies(s, fileSelection(2, 0)));
    EXPECT_TRUE(menuActionApplies(s, fileSelection(0, 2)));
    EXPECT_TRUE(menuActionApplies(s, fileSelection(1, 1)));
}

TEST(MenuActionResolverTest, FilesOnlyRejectsMixedSelection)
{
    MenuActionSpec s = spec(ActionTarget::FilesOnly, ActionArity::Any);
    EXPECT_FALSE(menuActionApplies(s, fileSelection(1, 1)));
}

TEST(MenuActionResolverTest, FilesOnlyRejectsFoldersOnlySelection)
{
    MenuActionSpec s = spec(ActionTarget::FilesOnly, ActionArity::Any);
    EXPECT_FALSE(menuActionApplies(s, fileSelection(0, 2)));
}

TEST(MenuActionResolverTest, FilesOnlyAcceptsMultipleFiles)
{
    MenuActionSpec s = spec(ActionTarget::FilesOnly, ActionArity::Any);
    EXPECT_TRUE(menuActionApplies(s, fileSelection(3, 0)));
}

TEST(MenuActionResolverTest, FoldersOnlyRejectsMixedSelection)
{
    MenuActionSpec s = spec(ActionTarget::FoldersOnly, ActionArity::Any);
    EXPECT_FALSE(menuActionApplies(s, fileSelection(1, 1)));
}

TEST(MenuActionResolverTest, FoldersOnlyAcceptsMultipleFolders)
{
    MenuActionSpec s = spec(ActionTarget::FoldersOnly, ActionArity::Any);
    EXPECT_TRUE(menuActionApplies(s, fileSelection(0, 3)));
}

TEST(MenuActionResolverTest, SingleOnlyAcceptsOneItem)
{
    MenuActionSpec s = spec(ActionTarget::Any, ActionArity::SingleOnly);
    EXPECT_TRUE(menuActionApplies(s, fileSelection(1, 0)));
}

TEST(MenuActionResolverTest, SingleOnlyRejectsTwoItems)
{
    MenuActionSpec s = spec(ActionTarget::Any, ActionArity::SingleOnly);
    EXPECT_FALSE(menuActionApplies(s, fileSelection(2, 0)));
}

TEST(MenuActionResolverTest, MultiOnlyRejectsOneItem)
{
    MenuActionSpec s = spec(ActionTarget::Any, ActionArity::MultiOnly);
    EXPECT_FALSE(menuActionApplies(s, fileSelection(1, 0)));
}

TEST(MenuActionResolverTest, MultiOnlyAcceptsTwoItems)
{
    MenuActionSpec s = spec(ActionTarget::Any, ActionArity::MultiOnly);
    EXPECT_TRUE(menuActionApplies(s, fileSelection(2, 0)));
}

TEST(MenuActionResolverTest, TargetAndArityMustBothMatch)
{
    MenuActionSpec s = spec(ActionTarget::FilesOnly, ActionArity::MultiOnly);
    EXPECT_FALSE(menuActionApplies(s, fileSelection(1, 0))); // arity fails
    EXPECT_TRUE(menuActionApplies(s, fileSelection(2, 0)));  // both match
}

TEST(MenuActionResolverTest, SiteMustMatchEvenWhenTargetAndArityDo)
{
    // Site is checked first and independently: a spec that would otherwise
    // apply is invisible at a site it doesn't belong to.
    MenuActionSpec s = spec(ActionTarget::Any, ActionArity::Any);
    EXPECT_TRUE(menuActionApplies(s, fileSelection(1, 0)));
    EXPECT_FALSE(menuActionApplies(s, folderTargetContext(MenuSite::FolderRow)));
    EXPECT_FALSE(menuActionApplies(s, folderTargetContext(MenuSite::FolderBackground)));
}

TEST(MenuActionResolverTest, FolderTargetContextSynthesizesExactlyOneFolder)
{
    // This is what lets the fixed-target sites reuse the selection-shaped
    // target/arity axes unchanged -- FoldersOnly and SingleOnly are satisfied
    // by construction, and the empty-selection short circuit can never fire.
    MenuContext ctx = folderTargetContext(MenuSite::FolderRow);
    EXPECT_EQ(ctx.site, MenuSite::FolderRow);
    EXPECT_EQ(ctx.selection.fileCount, 0);
    EXPECT_EQ(ctx.selection.folderCount, 1);
}

TEST(MenuActionResolverTest, PreservesTableOrder)
{
    std::vector<MenuActionSpec> specs = {
        spec(ActionTarget::Any, ActionArity::Any),
        spec(ActionTarget::FilesOnly, ActionArity::Any),
    };
    std::vector<MenuAction> result = resolveMenuActions(fileSelection(1, 0), specs);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], MenuAction::Download);
    EXPECT_EQ(result[1], MenuAction::Download);
}

TEST(MenuActionResolverTest, SkipsNonMatchingSpecs)
{
    std::vector<MenuActionSpec> specs = {
        spec(ActionTarget::FoldersOnly, ActionArity::Any),
        spec(ActionTarget::FilesOnly, ActionArity::Any),
    };
    std::vector<MenuAction> result = resolveMenuActions(fileSelection(1, 0), specs);
    ASSERT_EQ(result.size(), 1u);
}

TEST(MenuActionResolverTest, EmptySelectionYieldsNoActions)
{
    std::vector<MenuActionSpec> specs = {spec(ActionTarget::Any, ActionArity::Any)};
    EXPECT_TRUE(resolveMenuActions(fileSelection(0, 0), specs).empty());
}

TEST(MenuActionResolverTest, DefaultTableOffersDownloadForSingleFile)
{
    std::vector<MenuAction> result = resolveMenuActions(fileSelection(1, 0));
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], MenuAction::Download);
    EXPECT_EQ(result[1], MenuAction::Rename);
    EXPECT_EQ(result[2], MenuAction::MoveToRubbish);
}

TEST(MenuActionResolverTest, DefaultTableOffersDownloadForMultipleFiles)
{
    std::vector<MenuAction> result = resolveMenuActions(fileSelection(3, 0));
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], MenuAction::Download);
    EXPECT_EQ(result[1], MenuAction::MoveToRubbish);
}

TEST(MenuActionResolverTest, DefaultTableOffersNoDownloadWhenSelectionContainsAFolder)
{
    std::vector<MenuAction> result = resolveMenuActions(fileSelection(1, 1));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], MenuAction::MoveToRubbish);
}

TEST(MenuActionResolverTest, DefaultTableOffersNoDownloadForFoldersOnlySelection)
{
    std::vector<MenuAction> result = resolveMenuActions(fileSelection(0, 2));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], MenuAction::MoveToRubbish);
}

TEST(MenuActionResolverTest, DefaultTableOffersNothingForEmptySelection)
{
    EXPECT_TRUE(resolveMenuActions(fileSelection(0, 0)).empty());
}

TEST(MenuActionResolverTest, DownloadIdIsStable)
{
    EXPECT_STREQ(menuActionId(MenuAction::Download), "download");
}

TEST(MenuActionResolverTest, OpenInNewTabIdIsStable)
{
    EXPECT_STREQ(menuActionId(MenuAction::OpenInNewTab), "openInNewTab");
}

TEST(MenuActionResolverTest, DefaultTableOffersOpenInNewTabForSingleFolder)
{
    std::vector<MenuAction> result = resolveMenuActions(fileSelection(0, 1));
    ASSERT_EQ(result.size(), 4u);
    EXPECT_EQ(result[0], MenuAction::OpenInNewTab);
    EXPECT_EQ(result[1], MenuAction::TogglePin);
    EXPECT_EQ(result[2], MenuAction::Rename);
    EXPECT_EQ(result[3], MenuAction::MoveToRubbish);
}

TEST(MenuActionResolverTest, TogglePinIdIsStable)
{
    // The only contract linking this enum value to ActionCatalog.qml's entry
    // table.
    EXPECT_STREQ(menuActionId(MenuAction::TogglePin), "togglePin");
}

TEST(MenuActionResolverTest, DefaultTableNeverOffersTogglePinForMultipleFolders)
{
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(0, 2)), MenuAction::TogglePin));
}

TEST(MenuActionResolverTest, DefaultTableNeverOffersTogglePinForASingleFile)
{
    // Only folders can be pinned; a single file still gets Download.
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(1, 0)), MenuAction::TogglePin));
}

TEST(MenuActionResolverTest, DefaultTableNeverOffersTogglePinForAMixedSelection)
{
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(1, 1)), MenuAction::TogglePin));
}

TEST(MenuActionResolverTest, DefaultTableNeverOffersOpenInNewTabForMultipleFolders)
{
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(0, 2)), MenuAction::OpenInNewTab));
}

TEST(MenuActionResolverTest, DefaultTableNeverOffersOpenInNewTabForASingleFile)
{
    // Single file: Download applies (FilesOnly), OpenInNewTab doesn't
    // (FoldersOnly).
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(1, 0)), MenuAction::OpenInNewTab));
}

TEST(MenuActionResolverTest, DefaultTableNeverOffersOpenInNewTabForAMixedSelection)
{
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(1, 1)), MenuAction::OpenInNewTab));
}

TEST(MenuActionResolverTest, RenameIdIsStable)
{
    // The only contract linking this enum value to ActionCatalog.qml's entry
    // table.
    EXPECT_STREQ(menuActionId(MenuAction::Rename), "rename");
}

TEST(MenuActionResolverTest, MoveToRubbishIdIsStable)
{
    EXPECT_STREQ(menuActionId(MenuAction::MoveToRubbish), "moveToRubbish");
}

TEST(MenuActionResolverTest, NewFolderIdIsStable)
{
    EXPECT_STREQ(menuActionId(MenuAction::NewFolder), "newFolder");
}

TEST(MenuActionResolverTest, DefaultTableOffersRenameForASingleFileOrFolder)
{
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(1, 0)), MenuAction::Rename));
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(0, 1)), MenuAction::Rename));
}

TEST(MenuActionResolverTest, DefaultTableNeverOffersRenameForAMultiSelection)
{
    // The requirement "no rename while several items are selected", expressed
    // entirely by the spec table's ActionArity::SingleOnly.
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(2, 0)), MenuAction::Rename));
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(0, 2)), MenuAction::Rename));
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(1, 1)), MenuAction::Rename));
}

TEST(MenuActionResolverTest, DefaultTableOffersMoveToRubbishForEveryNonEmptySelection)
{
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(1, 0)), MenuAction::MoveToRubbish));
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(0, 1)), MenuAction::MoveToRubbish));
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(3, 2)), MenuAction::MoveToRubbish));
}

TEST(MenuActionResolverTest, DefaultTableNeverOffersRenameOrMoveToRubbishForAnEmptySelection)
{
    EXPECT_TRUE(resolveMenuActions(fileSelection(0, 0)).empty());
}

TEST(MenuActionResolverTest, DefaultTableOffersOnlyNewFolderOnAFolderBackground)
{
    std::vector<MenuAction> result =
        resolveMenuActions(folderTargetContext(MenuSite::FolderBackground));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], MenuAction::NewFolder);
}

TEST(MenuActionResolverTest, DefaultTableOffersOpenInNewTabAndTogglePinOnAFolderRow)
{
    std::vector<MenuAction> result = resolveMenuActions(folderTargetContext(MenuSite::FolderRow));
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], MenuAction::OpenInNewTab);
    EXPECT_EQ(result[1], MenuAction::TogglePin);
}

TEST(MenuActionResolverTest, DefaultTableNeverOffersNewFolderAtTheOtherSites)
{
    // NewFolder targets the folder a view is showing, not anything selected
    // or clicked, so it must not leak into the selection or row menus.
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(0, 1)), MenuAction::NewFolder));
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(1, 0)), MenuAction::NewFolder));
    EXPECT_FALSE(contains(resolveMenuActions(folderTargetContext(MenuSite::FolderRow)),
                          MenuAction::NewFolder));
}

TEST(MenuActionResolverTest, SharedActionsKeepTheSameRelativeOrderAcrossSites)
{
    // The whole point of a single global vocabulary: OpenInNewTab before
    // TogglePin no matter which menu you opened. Regression test for the
    // hand-synchronized ordering the old hardcoded FolderPinMenu.qml needed.
    const std::vector<MenuAction> selection = resolveMenuActions(fileSelection(0, 1));
    const std::vector<MenuAction> row =
        resolveMenuActions(folderTargetContext(MenuSite::FolderRow));

    std::vector<MenuAction> sharedInSelection;
    for (MenuAction action : selection)
    {
        if (contains(row, action))
            sharedInSelection.push_back(action);
    }
    EXPECT_EQ(sharedInSelection, row);
}
