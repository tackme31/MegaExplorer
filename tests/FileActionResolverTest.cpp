#include "core/FileActionResolver.h"

#include <gtest/gtest.h>

namespace
{

SelectionSummary summary(int files, int folders)
{
    SelectionSummary s;
    s.fileCount = files;
    s.folderCount = folders;
    return s;
}

FileActionSpec spec(ActionTarget target, ActionArity arity)
{
    // The action value itself never matters to fileActionApplies -- only
    // target/arity are inspected -- so every synthetic spec here reuses the
    // one real FileAction value that exists today.
    return FileActionSpec{FileAction::Download, target, arity};
}

const ActionTarget kAllTargets[] = {
    ActionTarget::Any, ActionTarget::FilesOnly, ActionTarget::FoldersOnly};
const ActionArity kAllArities[] = {
    ActionArity::Any, ActionArity::SingleOnly, ActionArity::MultiOnly};

} // namespace

TEST(FileActionResolverTest, EverySpecRejectsEmptySelection)
{
    for (ActionTarget target : kAllTargets)
    {
        for (ActionArity arity : kAllArities)
        {
            EXPECT_FALSE(fileActionApplies(spec(target, arity), summary(0, 0)))
                << "target=" << static_cast<int>(target) << " arity=" << static_cast<int>(arity);
        }
    }
}

TEST(FileActionResolverTest, AnyTargetAcceptsFilesFoldersAndMixed)
{
    FileActionSpec s = spec(ActionTarget::Any, ActionArity::Any);
    EXPECT_TRUE(fileActionApplies(s, summary(2, 0)));
    EXPECT_TRUE(fileActionApplies(s, summary(0, 2)));
    EXPECT_TRUE(fileActionApplies(s, summary(1, 1)));
}

TEST(FileActionResolverTest, FilesOnlyRejectsMixedSelection)
{
    FileActionSpec s = spec(ActionTarget::FilesOnly, ActionArity::Any);
    EXPECT_FALSE(fileActionApplies(s, summary(1, 1)));
}

TEST(FileActionResolverTest, FilesOnlyRejectsFoldersOnlySelection)
{
    FileActionSpec s = spec(ActionTarget::FilesOnly, ActionArity::Any);
    EXPECT_FALSE(fileActionApplies(s, summary(0, 2)));
}

TEST(FileActionResolverTest, FilesOnlyAcceptsMultipleFiles)
{
    FileActionSpec s = spec(ActionTarget::FilesOnly, ActionArity::Any);
    EXPECT_TRUE(fileActionApplies(s, summary(3, 0)));
}

TEST(FileActionResolverTest, FoldersOnlyRejectsMixedSelection)
{
    FileActionSpec s = spec(ActionTarget::FoldersOnly, ActionArity::Any);
    EXPECT_FALSE(fileActionApplies(s, summary(1, 1)));
}

TEST(FileActionResolverTest, FoldersOnlyAcceptsMultipleFolders)
{
    FileActionSpec s = spec(ActionTarget::FoldersOnly, ActionArity::Any);
    EXPECT_TRUE(fileActionApplies(s, summary(0, 3)));
}

TEST(FileActionResolverTest, SingleOnlyAcceptsOneItem)
{
    FileActionSpec s = spec(ActionTarget::Any, ActionArity::SingleOnly);
    EXPECT_TRUE(fileActionApplies(s, summary(1, 0)));
}

TEST(FileActionResolverTest, SingleOnlyRejectsTwoItems)
{
    FileActionSpec s = spec(ActionTarget::Any, ActionArity::SingleOnly);
    EXPECT_FALSE(fileActionApplies(s, summary(2, 0)));
}

TEST(FileActionResolverTest, MultiOnlyRejectsOneItem)
{
    FileActionSpec s = spec(ActionTarget::Any, ActionArity::MultiOnly);
    EXPECT_FALSE(fileActionApplies(s, summary(1, 0)));
}

TEST(FileActionResolverTest, MultiOnlyAcceptsTwoItems)
{
    FileActionSpec s = spec(ActionTarget::Any, ActionArity::MultiOnly);
    EXPECT_TRUE(fileActionApplies(s, summary(2, 0)));
}

TEST(FileActionResolverTest, TargetAndArityMustBothMatch)
{
    FileActionSpec s = spec(ActionTarget::FilesOnly, ActionArity::MultiOnly);
    EXPECT_FALSE(fileActionApplies(s, summary(1, 0))); // arity fails
    EXPECT_TRUE(fileActionApplies(s, summary(2, 0)));  // both match
}

TEST(FileActionResolverTest, PreservesTableOrder)
{
    std::vector<FileActionSpec> specs = {
        spec(ActionTarget::Any, ActionArity::Any),
        spec(ActionTarget::FilesOnly, ActionArity::Any),
    };
    std::vector<FileAction> result = resolveFileActions(summary(1, 0), specs);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], FileAction::Download);
    EXPECT_EQ(result[1], FileAction::Download);
}

TEST(FileActionResolverTest, SkipsNonMatchingSpecs)
{
    std::vector<FileActionSpec> specs = {
        spec(ActionTarget::FoldersOnly, ActionArity::Any),
        spec(ActionTarget::FilesOnly, ActionArity::Any),
    };
    std::vector<FileAction> result = resolveFileActions(summary(1, 0), specs);
    ASSERT_EQ(result.size(), 1u);
}

TEST(FileActionResolverTest, EmptySelectionYieldsNoActions)
{
    std::vector<FileActionSpec> specs = {spec(ActionTarget::Any, ActionArity::Any)};
    EXPECT_TRUE(resolveFileActions(summary(0, 0), specs).empty());
}

TEST(FileActionResolverTest, DefaultTableOffersDownloadForSingleFile)
{
    std::vector<FileAction> result = resolveFileActions(summary(1, 0));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], FileAction::Download);
}

TEST(FileActionResolverTest, DefaultTableOffersDownloadForMultipleFiles)
{
    std::vector<FileAction> result = resolveFileActions(summary(3, 0));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], FileAction::Download);
}

TEST(FileActionResolverTest, DefaultTableOffersNothingWhenSelectionContainsAFolder)
{
    EXPECT_TRUE(resolveFileActions(summary(1, 1)).empty());
}

TEST(FileActionResolverTest, DefaultTableOffersNothingForFoldersOnlySelection)
{
    EXPECT_TRUE(resolveFileActions(summary(0, 2)).empty());
}

TEST(FileActionResolverTest, DefaultTableOffersNothingForEmptySelection)
{
    EXPECT_TRUE(resolveFileActions(summary(0, 0)).empty());
}

TEST(FileActionResolverTest, DownloadIdIsStable)
{
    EXPECT_STREQ(fileActionId(FileAction::Download), "download");
}
