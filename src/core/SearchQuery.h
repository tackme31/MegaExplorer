#pragma once

#include <string>
#include <vector>

// What the search box's text asks for: `tag:term` words go to the tag search, the
// rest stays one name pattern. `tag:"two words"` keeps a space inside a term.
struct SearchQuery
{
    std::string name;
    std::vector<std::string> tags;
};

SearchQuery parseSearchQuery(const std::string& text);
