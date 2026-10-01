#include "SearchQuery.h"

#include <cctype>
#include <string_view>

namespace
{

constexpr std::string_view kTagPrefix = "tag:";
// U+3000, which a Japanese IME types for the space key.
constexpr std::string_view kIdeographicSpace = "\xE3\x80\x80";

std::size_t spaceLengthAt(std::string_view text, std::size_t pos)
{
    if (text[pos] == ' ' || text[pos] == '\t')
        return 1;
    if (text.substr(pos, kIdeographicSpace.size()) == kIdeographicSpace)
        return kIdeographicSpace.size();
    return 0;
}

std::size_t wordEnd(std::string_view text, std::size_t pos)
{
    while (pos < text.size() && spaceLengthAt(text, pos) == 0)
        ++pos;
    return pos;
}

bool startsWithTagPrefix(std::string_view text, std::size_t pos)
{
    if (text.size() - pos < kTagPrefix.size())
        return false;
    for (std::size_t i = 0; i < kTagPrefix.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(text[pos + i])) != kTagPrefix[i])
            return false;
    }
    return true;
}

} // namespace

SearchQuery parseSearchQuery(const std::string& text)
{
    SearchQuery query;
    const std::string_view view(text);
    std::size_t pos = 0;
    while (pos < view.size())
    {
        if (const std::size_t space = spaceLengthAt(view, pos))
        {
            pos += space;
            continue;
        }

        if (startsWithTagPrefix(view, pos))
        {
            std::size_t start = pos + kTagPrefix.size();
            std::size_t end = 0;
            if (start < view.size() && view[start] == '"')
            {
                ++start;
                const std::size_t close = view.find('"', start);
                end = close == std::string_view::npos ? view.size() : close;
                pos = close == std::string_view::npos ? view.size() : close + 1;
            }
            else
            {
                end = wordEnd(view, start);
                pos = end;
            }
            if (end > start)
                query.tags.emplace_back(view.substr(start, end - start));
            continue;
        }

        const std::size_t end = wordEnd(view, pos);
        if (!query.name.empty())
            query.name += ' ';
        query.name.append(view.substr(pos, end - pos));
        pos = end;
    }
    return query;
}
