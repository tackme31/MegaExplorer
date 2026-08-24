#include "core/MenuActionResolver.h"

#include <algorithm>
#include <gtest/gtest.h>

namespace
{

// The site every target/arity case below is about: FileSelection is the only
// site whose target actually varies, which is why those two axes exist at all.
MenuContext fileSelection(int files, int folders, ViewKind kind = ViewKind::CloudDrive)
{
    MenuContext ctx;
    ctx.kind = kind;
    ctx.site = MenuSite::FileSelection;
    ctx.selection.fileCount = files;
    ctx.selection.folderCount = folders;
    return ctx;
}

// Cloud Drive is what the site/target/arity cases are all about; the scope cases
// below name the kind themselves.
MenuContext folderTarget(MenuSite site, ViewKind kind = ViewKind::CloudDrive)
{
    return folderTargetContext(site, kind);
}

MenuActionSpec spec(ActionTarget target,
                    ActionArity arity,
                    std::vector<ViewKind> scopes = {ViewKind::CloudDrive, ViewKind::Favourites})
{
    // The action value itself never matters to menuActionApplies -- only
    // site/scope/target/arity are inspected -- so every synthetic spec here reuses
    // one real MenuAction value.
    return MenuActionSpec{
        MenuAction::Download, {MenuSite::FileSelection}, std::move(scopes), target, arity};
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
    EXPECT_FALSE(menuActionApplies(s, folderTarget(MenuSite::FolderRow)));
    EXPECT_FALSE(menuActionApplies(s, folderTarget(MenuSite::FolderBackground)));
}

TEST(MenuActionResolverTest, ScopeMustMatchEvenWhenEverythingElseDoes)
{
    // Same independence as the site check above, along the view-kind axis.
    MenuActionSpec s = spec(ActionTarget::Any, ActionArity::Any, {ViewKind::CloudDrive});
    EXPECT_TRUE(menuActionApplies(s, fileSelection(1, 0)));
    EXPECT_FALSE(menuActionApplies(s, fileSelection(1, 0, ViewKind::Favourites)));
}

TEST(MenuActionResolverTest, ScopeAdmitsEveryKindItLists)
{
    MenuActionSpec s = spec(ActionTarget::Any, ActionArity::Any);
    EXPECT_TRUE(menuActionApplies(s, fileSelection(1, 0)));
    EXPECT_TRUE(menuActionApplies(s, fileSelection(1, 0, ViewKind::Favourites)));
}

TEST(MenuActionResolverTest, CrossFolderOnlyNeedsACrossFolderListing)
{
    // The third restriction, and independent of the other two the same way site and
    // scope are: everything else about this context matches.
    MenuActionSpec s = spec(ActionTarget::Any, ActionArity::Any);
    s.crossFolderOnly = true;
    EXPECT_FALSE(menuActionApplies(s, fileSelection(1, 0)));

    MenuContext ctx = fileSelection(1, 0);
    ctx.crossFolderListing = true;
    EXPECT_TRUE(menuActionApplies(s, ctx));
}

TEST(MenuActionResolverTest, OrdinaryActionsIgnoreTheCrossFolderFlag)
{
    // The flag restricts only the specs that ask for it -- a searching listing must
    // not start offering or withholding anything else.
    MenuContext ctx = fileSelection(1, 0);
    ctx.crossFolderListing = true;
    EXPECT_EQ(resolveMenuActions(fileSelection(1, 0)).size() + 1, resolveMenuActions(ctx).size());
}

TEST(MenuActionResolverTest, GoToFolderIsOfferedOnlyWhereTheParentDiffers)
{
    // A plain folder listing already *is* the containing folder, so the row would
    // navigate to where the user already stands.
    const auto offers = [](const MenuContext& ctx) {
        const std::vector<MenuAction> actions = resolveMenuActions(ctx);
        return std::find(actions.begin(), actions.end(), MenuAction::GoToFolder) != actions.end();
    };

    EXPECT_FALSE(offers(fileSelection(1, 0)));

    MenuContext searching = fileSelection(1, 0);
    searching.crossFolderListing = true;
    EXPECT_TRUE(offers(searching));

    MenuContext favourites = fileSelection(1, 0, ViewKind::Favourites);
    favourites.crossFolderListing = true;
    EXPECT_TRUE(offers(favourites));

    // Two items have no one containing folder to go to.
    MenuContext multiple = fileSelection(2, 0);
    multiple.crossFolderListing = true;
    EXPECT_FALSE(offers(multiple));

    // The bin is flat and its rows' parents are gone; Restore answers that instead.
    MenuContext rubbish = fileSelection(1, 0, ViewKind::Rubbish);
    rubbish.crossFolderListing = true;
    EXPECT_FALSE(offers(rubbish));
}

TEST(MenuActionResolverTest, FolderTargetContextCarriesTheViewKind)
{
    // The fixed-target sites have no selection to read a kind off, so the caller's
    // kind has to survive the synthesis -- otherwise a favourites background would
    // resolve as Cloud Drive's.
    EXPECT_EQ(folderTargetContext(MenuSite::FolderBackground, ViewKind::Favourites).kind,
              ViewKind::Favourites);
}

TEST(MenuActionResolverTest, FolderTargetContextSynthesizesExactlyOneFolder)
{
    // This is what lets the fixed-target sites reuse the selection-shaped
    // target/arity axes unchanged -- FoldersOnly and SingleOnly are satisfied
    // by construction, and the empty-selection short circuit can never fire.
    MenuContext ctx = folderTarget(MenuSite::FolderRow);
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
    ASSERT_EQ(result.size(), 11u);
    EXPECT_EQ(result[0], MenuAction::Download);
    EXPECT_EQ(result[1], MenuAction::OpenLocalFile);
    EXPECT_EQ(result[2], MenuAction::OpenLocalLocation);
    EXPECT_EQ(result[3], MenuAction::ToggleFavourite);
    EXPECT_EQ(result[4], MenuAction::CopyLink);
    EXPECT_EQ(result[5], MenuAction::RemoveLink);
    EXPECT_EQ(result[6], MenuAction::Cut);
    EXPECT_EQ(result[7], MenuAction::Copy);
    EXPECT_EQ(result[8], MenuAction::Rename);
    EXPECT_EQ(result[9], MenuAction::MoveToRubbish);
    EXPECT_EQ(result[10], MenuAction::Properties);
}

TEST(MenuActionResolverTest, DefaultTableOffersDownloadForMultipleFiles)
{
    std::vector<MenuAction> result = resolveMenuActions(fileSelection(3, 0));
    ASSERT_EQ(result.size(), 4u);
    EXPECT_EQ(result[0], MenuAction::Download);
    EXPECT_EQ(result[1], MenuAction::Cut);
    EXPECT_EQ(result[2], MenuAction::Copy);
    EXPECT_EQ(result[3], MenuAction::MoveToRubbish);
}

TEST(MenuActionResolverTest, DefaultTableOffersNoDownloadWhenSelectionContainsAFolder)
{
    std::vector<MenuAction> result = resolveMenuActions(fileSelection(1, 1));
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], MenuAction::Cut);
    EXPECT_EQ(result[1], MenuAction::Copy);
    EXPECT_EQ(result[2], MenuAction::MoveToRubbish);
}

TEST(MenuActionResolverTest, DefaultTableOffersNoDownloadForFoldersOnlySelection)
{
    std::vector<MenuAction> result = resolveMenuActions(fileSelection(0, 2));
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], MenuAction::Cut);
    EXPECT_EQ(result[1], MenuAction::Copy);
    EXPECT_EQ(result[2], MenuAction::MoveToRubbish);
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

TEST(MenuActionResolverTest, OpenLocalFileIdIsStable)
{
    EXPECT_STREQ(menuActionId(MenuAction::OpenLocalFile), "openLocalFile");
}

TEST(MenuActionResolverTest, DefaultTableOffersOpenLocalFileForASingleFileOnly)
{
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(1, 0)), MenuAction::OpenLocalFile));
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(1, 0, ViewKind::Favourites)),
                         MenuAction::OpenLocalFile));
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(1, 0, ViewKind::Recents)),
                         MenuAction::OpenLocalFile));
}

TEST(MenuActionResolverTest, DefaultTableWithholdsOpenLocalFileWhereNoOneFileIsMeant)
{
    // A folder differs from OpenLocalLocation's rule: the shell's answer for one is
    // an Explorer window, which is what revealing already does. The rest match --
    // multi-selection, the bin, and the two fixed-target sites.
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(0, 1)), MenuAction::OpenLocalFile));
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(2, 0)), MenuAction::OpenLocalFile));
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(1, 0, ViewKind::Rubbish)),
                          MenuAction::OpenLocalFile));
    EXPECT_FALSE(
        contains(resolveMenuActions(folderTarget(MenuSite::FolderRow)), MenuAction::OpenLocalFile));
    EXPECT_FALSE(contains(resolveMenuActions(folderTarget(MenuSite::FolderBackground)),
                          MenuAction::OpenLocalFile));
}

TEST(MenuActionResolverTest, OpenLocalLocationIdIsStable)
{
    EXPECT_STREQ(menuActionId(MenuAction::OpenLocalLocation), "openLocalLocation");
}

TEST(MenuActionResolverTest, DefaultTableOffersOpenLocalLocationForASingleFileOrFolder)
{
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(1, 0)), MenuAction::OpenLocalLocation));
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(0, 1)), MenuAction::OpenLocalLocation));
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(1, 0, ViewKind::Recents)),
                         MenuAction::OpenLocalLocation));
}

TEST(MenuActionResolverTest, DefaultTableWithholdsOpenLocalLocationWhereNoOneItemIsMeant)
{
    // Multi-selection: Explorer selects one item per window, so the rest would be
    // silently dropped. Rubbish: a binned node has no counterpart on disk. The two
    // fixed-target sites: the folder a view shows is reached by its own breadcrumb,
    // not by revealing a row.
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(2, 0)), MenuAction::OpenLocalLocation));
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(1, 0, ViewKind::Rubbish)),
                          MenuAction::OpenLocalLocation));
    EXPECT_FALSE(contains(resolveMenuActions(folderTarget(MenuSite::FolderRow)),
                          MenuAction::OpenLocalLocation));
    EXPECT_FALSE(contains(resolveMenuActions(folderTarget(MenuSite::FolderBackground)),
                          MenuAction::OpenLocalLocation));
}

TEST(MenuActionResolverTest, DefaultTableOffersOpenInNewTabForSingleFolder)
{
    std::vector<MenuAction> result = resolveMenuActions(fileSelection(0, 1));
    ASSERT_EQ(result.size(), 11u);
    EXPECT_EQ(result[0], MenuAction::OpenLocalLocation);
    EXPECT_EQ(result[1], MenuAction::OpenInNewTab);
    EXPECT_EQ(result[2], MenuAction::TogglePin);
    EXPECT_EQ(result[3], MenuAction::ToggleFavourite);
    EXPECT_EQ(result[4], MenuAction::CopyLink);
    EXPECT_EQ(result[5], MenuAction::RemoveLink);
    EXPECT_EQ(result[6], MenuAction::Cut);
    EXPECT_EQ(result[7], MenuAction::Copy);
    EXPECT_EQ(result[8], MenuAction::Rename);
    EXPECT_EQ(result[9], MenuAction::MoveToRubbish);
    EXPECT_EQ(result[10], MenuAction::Properties);
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

TEST(MenuActionResolverTest, ToggleFavouriteIdIsStable)
{
    EXPECT_STREQ(menuActionId(MenuAction::ToggleFavourite), "toggleFavourite");
}

TEST(MenuActionResolverTest, DefaultTableOffersToggleFavouriteForASingleFileOrFolder)
{
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(1, 0)), MenuAction::ToggleFavourite));
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(0, 1)), MenuAction::ToggleFavourite));
}

TEST(MenuActionResolverTest, DefaultTableNeverOffersToggleFavouriteForMultipleItems)
{
    // Not a target restriction but an arity one: with a mixed selection there is
    // no single "add"/"remove" label to show.
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(2, 0)), MenuAction::ToggleFavourite));
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(0, 2)), MenuAction::ToggleFavourite));
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(1, 1)), MenuAction::ToggleFavourite));
}

TEST(MenuActionResolverTest, DefaultTableNeverOffersToggleFavouriteOnFixedTargetSites)
{
    EXPECT_FALSE(contains(resolveMenuActions(folderTarget(MenuSite::FolderBackground)),
                          MenuAction::ToggleFavourite));
    EXPECT_FALSE(contains(resolveMenuActions(folderTarget(MenuSite::FolderRow)),
                          MenuAction::ToggleFavourite));
}

TEST(MenuActionResolverTest, LinkActionIdsAreStable)
{
    EXPECT_STREQ(menuActionId(MenuAction::CopyLink), "copyLink");
    EXPECT_STREQ(menuActionId(MenuAction::RemoveLink), "removeLink");
}

TEST(MenuActionResolverTest, DefaultTableOffersBothLinkActionsForASingleFileOrFolder)
{
    // Both, always, in either direction: the listing carries no export flag, so
    // neither entry can be conditioned on whether a link already exists.
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(1, 0)), MenuAction::CopyLink));
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(1, 0)), MenuAction::RemoveLink));
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(0, 1)), MenuAction::CopyLink));
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(0, 1)), MenuAction::RemoveLink));
}

TEST(MenuActionResolverTest, DefaultTableNeverOffersLinkActionsForMultipleItems)
{
    // Arity, not target: the clipboard holds one string, so a multi-selection
    // could only leave the last link on it.
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(2, 0)), MenuAction::CopyLink));
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(1, 1)), MenuAction::CopyLink));
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(2, 0)), MenuAction::RemoveLink));
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(1, 1)), MenuAction::RemoveLink));
}

TEST(MenuActionResolverTest, DefaultTableNeverOffersLinkActionsInTheRubbishBin)
{
    EXPECT_FALSE(
        contains(resolveMenuActions(fileSelection(1, 0, ViewKind::Rubbish)), MenuAction::CopyLink));
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(1, 0, ViewKind::Rubbish)),
                          MenuAction::RemoveLink));
}

TEST(MenuActionResolverTest, DefaultTableNeverOffersLinkActionsOnFixedTargetSites)
{
    EXPECT_FALSE(
        contains(resolveMenuActions(folderTarget(MenuSite::FolderRow)), MenuAction::CopyLink));
    EXPECT_FALSE(contains(resolveMenuActions(folderTarget(MenuSite::FolderBackground)),
                          MenuAction::RemoveLink));
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

TEST(MenuActionResolverTest, DefaultTableOffersTheFiveBackgroundActionsInMenuOrder)
{
    std::vector<MenuAction> result = resolveMenuActions(folderTarget(MenuSite::FolderBackground));
    ASSERT_EQ(result.size(), 5u);
    EXPECT_EQ(result[0], MenuAction::NewFolder);
    EXPECT_EQ(result[1], MenuAction::Paste);
    EXPECT_EQ(result[2], MenuAction::SelectAll);
    EXPECT_EQ(result[3], MenuAction::Refresh);
    EXPECT_EQ(result[4], MenuAction::Properties);
}

TEST(MenuActionResolverTest, DefaultTableOffersCutAndCopyForEveryNonEmptySelection)
{
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(1, 0)), MenuAction::Cut));
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(0, 1)), MenuAction::Copy));
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(3, 2)), MenuAction::Cut));
    EXPECT_TRUE(contains(resolveMenuActions(fileSelection(3, 2)), MenuAction::Copy));
}

TEST(MenuActionResolverTest, DefaultTableNeverOffersPasteAtTheSelectionOrRowSites)
{
    // Paste targets the folder a view is showing, like NewFolder -- and unlike
    // 14a's drop targets, it deliberately doesn't reach folder rows (Phase 23).
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(0, 1)), MenuAction::Paste));
    EXPECT_FALSE(
        contains(resolveMenuActions(folderTarget(MenuSite::FolderRow)), MenuAction::Paste));
}

TEST(MenuActionResolverTest, DefaultTableNeverOffersCutOrCopyOnABackgroundOrRow)
{
    const std::vector<MenuAction> background =
        resolveMenuActions(folderTarget(MenuSite::FolderBackground));
    const std::vector<MenuAction> row = resolveMenuActions(folderTarget(MenuSite::FolderRow));
    EXPECT_FALSE(contains(background, MenuAction::Cut));
    EXPECT_FALSE(contains(background, MenuAction::Copy));
    EXPECT_FALSE(contains(row, MenuAction::Cut));
    EXPECT_FALSE(contains(row, MenuAction::Copy));
}

TEST(MenuActionResolverTest, ClipboardActionIdsAreStable)
{
    // The only contract linking these enum values to ActionCatalog.qml.
    EXPECT_STREQ(menuActionId(MenuAction::Cut), "cut");
    EXPECT_STREQ(menuActionId(MenuAction::Copy), "copy");
    EXPECT_STREQ(menuActionId(MenuAction::Paste), "paste");
    EXPECT_STREQ(menuActionId(MenuAction::SelectAll), "selectAll");
    EXPECT_STREQ(menuActionId(MenuAction::Refresh), "refresh");
}

TEST(MenuActionResolverTest, DefaultTableOffersOpenInNewTabTogglePinAndRefreshOnAFolderRow)
{
    std::vector<MenuAction> result = resolveMenuActions(folderTarget(MenuSite::FolderRow));
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], MenuAction::OpenInNewTab);
    EXPECT_EQ(result[1], MenuAction::TogglePin);
    // Reaches every FolderRow, including a Quick access pin that has no subtree to
    // re-read -- hiding it there is QML's job, not the resolver's.
    EXPECT_EQ(result[2], MenuAction::Refresh);
}

TEST(MenuActionResolverTest, DefaultTableNeverOffersNewFolderAtTheOtherSites)
{
    // NewFolder targets the folder a view is showing, not anything selected
    // or clicked, so it must not leak into the selection or row menus.
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(0, 1)), MenuAction::NewFolder));
    EXPECT_FALSE(contains(resolveMenuActions(fileSelection(1, 0)), MenuAction::NewFolder));
    EXPECT_FALSE(
        contains(resolveMenuActions(folderTarget(MenuSite::FolderRow)), MenuAction::NewFolder));
}

TEST(MenuActionResolverTest, SharedActionsKeepTheSameRelativeOrderAcrossSites)
{
    // The whole point of a single global vocabulary: OpenInNewTab before
    // TogglePin no matter which menu you opened. Regression test for the
    // hand-synchronized ordering the old hardcoded FolderPinMenu.qml needed.
    const std::vector<MenuAction> selection = resolveMenuActions(fileSelection(0, 1));
    const std::vector<MenuAction> row = resolveMenuActions(folderTarget(MenuSite::FolderRow));

    // Intersected in both directions rather than compared against `row` whole:
    // each site has entries the other never offers (Refresh is FolderRow's), and
    // this test is about the order of what they share, not about membership.
    std::vector<MenuAction> sharedInSelection;
    for (MenuAction action : selection)
    {
        if (contains(row, action))
            sharedInSelection.push_back(action);
    }
    std::vector<MenuAction> sharedInRow;
    for (MenuAction action : row)
    {
        if (contains(selection, action))
            sharedInRow.push_back(action);
    }
    EXPECT_FALSE(sharedInSelection.empty());
    EXPECT_EQ(sharedInSelection, sharedInRow);
}

TEST(MenuActionResolverTest, DefaultTableWithholdsCutAndMoveToRubbishInFavourites)
{
    // FAVOURITES_VIEW_SPEC.md 4.1: a favourites listing can rename and copy but
    // never move, and cut is a deferred move (its decision 1).
    const std::vector<MenuAction> result =
        resolveMenuActions(fileSelection(1, 0, ViewKind::Favourites));
    ASSERT_EQ(result.size(), 9u);
    EXPECT_EQ(result[0], MenuAction::Download);
    EXPECT_EQ(result[1], MenuAction::OpenLocalFile);
    EXPECT_EQ(result[2], MenuAction::OpenLocalLocation);
    EXPECT_EQ(result[3], MenuAction::ToggleFavourite);
    EXPECT_EQ(result[4], MenuAction::CopyLink);
    EXPECT_EQ(result[5], MenuAction::RemoveLink);
    EXPECT_EQ(result[6], MenuAction::Copy);
    EXPECT_EQ(result[7], MenuAction::Rename);
    EXPECT_EQ(result[8], MenuAction::Properties);
}

TEST(MenuActionResolverTest, DefaultTableStillOffersOpenInNewTabAndTogglePinInFavourites)
{
    // Both act on the folder the row names, which is a real node wherever it is
    // listed -- so neither has a reason to drop out.
    const std::vector<MenuAction> result =
        resolveMenuActions(fileSelection(0, 1, ViewKind::Favourites));
    EXPECT_TRUE(contains(result, MenuAction::OpenInNewTab));
    EXPECT_TRUE(contains(result, MenuAction::TogglePin));
}

TEST(MenuActionResolverTest, DefaultTableOffersOnlySelectAllAndRefreshOnAFavouritesBackground)
{
    // New folder and paste both need a destination folder, which a flat
    // cross-drive listing doesn't have.
    const std::vector<MenuAction> result =
        resolveMenuActions(folderTarget(MenuSite::FolderBackground, ViewKind::Favourites));
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], MenuAction::SelectAll);
    EXPECT_EQ(result[1], MenuAction::Refresh);
}

TEST(MenuActionResolverTest, DefaultTableTreatsRecentsExactlyAsFavourites)
{
    // Both are flat cross-drive queries, so the four actions needing a destination
    // folder drop out of both. Compared rather than re-listed: the point is that the
    // two stay identical, not what the list happens to be today.
    EXPECT_EQ(resolveMenuActions(fileSelection(1, 0, ViewKind::Recents)),
              resolveMenuActions(fileSelection(1, 0, ViewKind::Favourites)));
    EXPECT_EQ(resolveMenuActions(fileSelection(0, 1, ViewKind::Recents)),
              resolveMenuActions(fileSelection(0, 1, ViewKind::Favourites)));
    EXPECT_EQ(resolveMenuActions(folderTarget(MenuSite::FolderBackground, ViewKind::Recents)),
              resolveMenuActions(folderTarget(MenuSite::FolderBackground, ViewKind::Favourites)));
}

TEST(MenuActionResolverTest, MenuActionAllowedAgreesWithTheResolvedMenu)
{
    // The keyboard's entry point: same table, same answer, addressed by ID.
    EXPECT_TRUE(menuActionAllowed("moveToRubbish", fileSelection(1, 0)));
    EXPECT_FALSE(menuActionAllowed("moveToRubbish", fileSelection(1, 0, ViewKind::Favourites)));
    EXPECT_FALSE(menuActionAllowed("moveToRubbish", fileSelection(0, 0)));
    EXPECT_TRUE(menuActionAllowed("paste", folderTarget(MenuSite::FolderBackground)));
    EXPECT_FALSE(
        menuActionAllowed("paste", folderTarget(MenuSite::FolderBackground, ViewKind::Favourites)));
}

TEST(MenuActionResolverTest, PropertiesIdIsStable)
{
    EXPECT_STREQ(menuActionId(MenuAction::Properties), "properties");
}

TEST(MenuActionResolverTest, DefaultTableOffersPropertiesForOneItemInEveryView)
{
    // The one selection action the bin keeps: it only reads, and a binned node's
    // size and original location are exactly what gets looked up there.
    for (ViewKind kind :
         {ViewKind::CloudDrive, ViewKind::Favourites, ViewKind::Recents, ViewKind::Rubbish})
    {
        EXPECT_TRUE(contains(resolveMenuActions(fileSelection(1, 0, kind)), MenuAction::Properties))
            << static_cast<int>(kind);
        EXPECT_TRUE(contains(resolveMenuActions(fileSelection(0, 1, kind)), MenuAction::Properties))
            << static_cast<int>(kind);
        // One node, one dialog -- a multi-selection has nothing to describe.
        EXPECT_FALSE(
            contains(resolveMenuActions(fileSelection(2, 0, kind)), MenuAction::Properties))
            << static_cast<int>(kind);
    }
}

TEST(MenuActionResolverTest, DefaultTableOffersPropertiesOnACloudDriveBackgroundOnly)
{
    // The background's target is the folder on screen, which only the Cloud Drive
    // always has as a real node: the two flat listings synthesize a handle-less
    // location, and the bin's own top resolves to the Cloud Drive root instead.
    EXPECT_TRUE(contains(resolveMenuActions(folderTarget(MenuSite::FolderBackground)),
                         MenuAction::Properties));
    for (ViewKind kind : {ViewKind::Favourites, ViewKind::Recents, ViewKind::Rubbish})
    {
        EXPECT_FALSE(contains(resolveMenuActions(folderTarget(MenuSite::FolderBackground, kind)),
                              MenuAction::Properties))
            << static_cast<int>(kind);
    }
}

TEST(MenuActionResolverTest, DefaultTableNeverOffersPropertiesOnAFolderRow)
{
    // A tree or Quick-access row names a folder the breadcrumb reaches anyway, and
    // the dialog is one instance the file views own.
    EXPECT_FALSE(
        contains(resolveMenuActions(folderTarget(MenuSite::FolderRow)), MenuAction::Properties));
}

TEST(MenuActionResolverTest, MenuActionAllowedConsidersEverySpecForAnId)
{
    // Properties is the first action with a row per site; a first-match lookup would
    // answer the background's question from the selection's row and say no.
    EXPECT_TRUE(menuActionAllowed("properties", fileSelection(1, 0)));
    EXPECT_TRUE(menuActionAllowed("properties", folderTarget(MenuSite::FolderBackground)));
    EXPECT_FALSE(menuActionAllowed(
        "properties", folderTarget(MenuSite::FolderBackground, ViewKind::Favourites)));
}

TEST(MenuActionResolverTest, MenuActionAllowedRejectsAnUnknownId)
{
    EXPECT_FALSE(menuActionAllowed("noSuchAction", fileSelection(1, 0)));
    EXPECT_FALSE(menuActionAllowed("", fileSelection(1, 0)));
}
