#pragma once

// The facets the toolbar's advanced-search popup narrows a search by. Mapped onto
// MegaSearchFilter in src/mega, so src/core keeps its no-SDK rule; the values here
// are ours, not the SDK's integers.

enum class SearchNodeType
{
    Any,
    Files,
    Folders,
};

// A subset of MegaApi's FILE_TYPE_* set: the composite ones (ALL_DOCS,
// ALL_VISUAL_MEDIA) are left out because the individual entries already cover them.
enum class SearchCategory
{
    Any,
    Photo,
    Audio,
    Video,
    Document,
    Pdf,
    Presentation,
    Spreadsheet,
    Archive,
    Program,
    Other,
};

// Rolling windows ending "now", not calendar days: MegaSearchFilter takes two
// timestamps, and "since midnight" would need a timezone src/core has no access to.
enum class SearchTimeWindow
{
    Any,
    PastDay,
    PastWeek,
    PastMonth,
    PastYear,
};

struct SearchFilter
{
    SearchNodeType nodeType = SearchNodeType::Any;
    SearchCategory category = SearchCategory::Any;
    SearchTimeWindow createdWithin = SearchTimeWindow::Any;
    bool favouritesOnly = false;

    // "Narrows nothing", which is what lets an empty query still count as no search
    // at all -- SearchService refuses that pair rather than listing the whole drive.
    bool isDefault() const
    {
        return nodeType == SearchNodeType::Any && category == SearchCategory::Any &&
               createdWithin == SearchTimeWindow::Any && !favouritesOnly;
    }
};

inline bool operator==(const SearchFilter& lhs, const SearchFilter& rhs)
{
    return lhs.nodeType == rhs.nodeType && lhs.category == rhs.category &&
           lhs.createdWithin == rhs.createdWithin && lhs.favouritesOnly == rhs.favouritesOnly;
}

inline bool operator!=(const SearchFilter& lhs, const SearchFilter& rhs)
{
    return !(lhs == rhs);
}
