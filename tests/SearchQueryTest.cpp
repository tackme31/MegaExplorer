#include "core/SearchQuery.h"

#include <gtest/gtest.h>

namespace
{

std::vector<std::string> tags(std::initializer_list<const char*> list)
{
    return {list.begin(), list.end()};
}

} // namespace

TEST(SearchQueryTest, PlainTextIsAllName)
{
    const SearchQuery query = parseSearchQuery("holiday photos");
    EXPECT_EQ(query.name, "holiday photos");
    EXPECT_TRUE(query.tags.empty());
}

TEST(SearchQueryTest, TagWordsAreTakenOutOfTheName)
{
    const SearchQuery query = parseSearchQuery("miku tag:1girl photos tag:chara:hatsune_miku");
    EXPECT_EQ(query.name, "miku photos");
    EXPECT_EQ(query.tags, tags({"1girl", "chara:hatsune_miku"}));
}

TEST(SearchQueryTest, TagsAloneLeaveNoName)
{
    const SearchQuery query = parseSearchQuery("tag:blue_hair tag:rating:general");
    EXPECT_EQ(query.name, "");
    EXPECT_EQ(query.tags, tags({"blue_hair", "rating:general"}));
}

TEST(SearchQueryTest, ThePrefixIgnoresCase)
{
    EXPECT_EQ(parseSearchQuery("TAG:smile").tags, tags({"smile"}));
    EXPECT_EQ(parseSearchQuery("Tag:smile").tags, tags({"smile"}));
}

TEST(SearchQueryTest, QuotesKeepSpacesInsideATag)
{
    const SearchQuery query = parseSearchQuery("tag:\"my tag\" report");
    EXPECT_EQ(query.tags, tags({"my tag"}));
    EXPECT_EQ(query.name, "report");
}

TEST(SearchQueryTest, AnUnclosedQuoteRunsToTheEnd)
{
    EXPECT_EQ(parseSearchQuery("tag:\"my tag").tags, tags({"my tag"}));
}

TEST(SearchQueryTest, AnEmptyTagIsDropped)
{
    const SearchQuery query = parseSearchQuery("tag: tag:\"\" notes");
    EXPECT_TRUE(query.tags.empty());
    EXPECT_EQ(query.name, "notes");
}

TEST(SearchQueryTest, AWordMerelyContainingTagStaysInTheName)
{
    const SearchQuery query = parseSearchQuery("price-tag:2024 hashtag:x");
    EXPECT_EQ(query.name, "price-tag:2024 hashtag:x");
    EXPECT_TRUE(query.tags.empty());
}

TEST(SearchQueryTest, AnIdeographicSpaceSeparatesWords)
{
    const SearchQuery query = parseSearchQuery("\xE5\x86\x99\xE7\x9C\x9F\xE3\x80\x80tag:smile");
    EXPECT_EQ(query.name, "\xE5\x86\x99\xE7\x9C\x9F");
    EXPECT_EQ(query.tags, tags({"smile"}));
}

TEST(SearchQueryTest, RunsOfSpacesCollapseInTheName)
{
    EXPECT_EQ(parseSearchQuery("  a   b  ").name, "a b");
}
