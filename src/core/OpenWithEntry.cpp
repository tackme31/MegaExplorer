#include "OpenWithEntry.h"

#include "MenuActionResolver.h"

#include <cstddef>
#include <string>

namespace
{

// Hand-rolled rather than std::tolower: that one takes an int and is undefined for
// a negative char, which every byte above 0x7F is on this platform's signed char.
char asciiLower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalsIgnoringAsciiCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (asciiLower(a[i]) != asciiLower(b[i]))
            return false;
    }
    return true;
}

bool isSeparator(char c)
{
    return c == ',' || c == ';' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// "" for a name with no dot, and also for one ending in a dot -- neither has an
// extension to compare, and both then only match an empty extension list.
std::string_view extensionOf(const std::string& fileName)
{
    const std::size_t dot = fileName.rfind('.');
    if (dot == std::string::npos || dot + 1 == fileName.size())
        return {};
    return std::string_view(fileName).substr(dot + 1);
}

} // namespace

bool openWithExtensionMatches(const std::string& extensions, const std::string& fileName)
{
    const std::string_view suffix = extensionOf(fileName);

    bool sawToken = false;
    std::size_t i = 0;
    while (i < extensions.size())
    {
        if (isSeparator(extensions[i]))
        {
            ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < extensions.size() && !isSeparator(extensions[i]))
            ++i;
        std::string_view token = std::string_view(extensions).substr(start, i - start);
        // A wildcard is not parsed here: users write "*.jpg" out of habit, and
        // stripping the star keeps that spelling working alongside "jpg" and ".jpg".
        if (!token.empty() && token.front() == '*')
            token.remove_prefix(1);
        if (!token.empty() && token.front() == '.')
            token.remove_prefix(1);
        if (token.empty())
            continue;
        sawToken = true;
        if (!suffix.empty() && equalsIgnoringAsciiCase(token, suffix))
            return true;
    }

    return !sawToken;
}

std::string openWithCustomActionId(int index)
{
    return std::string(menuActionId(MenuAction::OpenWithCustom)) + ':' + std::to_string(index);
}

int openWithCustomIndex(std::string_view actionId)
{
    const std::string prefix = std::string(menuActionId(MenuAction::OpenWithCustom)) + ':';
    if (actionId.size() <= prefix.size() || actionId.substr(0, prefix.size()) != prefix)
        return -1;

    const std::string_view digits = actionId.substr(prefix.size());
    int value = 0;
    for (char c : digits)
    {
        if (c < '0' || c > '9')
            return -1;
        // Anything past a few thousand entries can only be a malformed ID, and
        // saturating here keeps the multiply below out of signed overflow.
        if (value > 100000)
            return -1;
        value = value * 10 + (c - '0');
    }
    return value;
}
