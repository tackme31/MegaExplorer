#include "FileKind.h"

#include <cctype>

std::string fileExtensionUppercased(const std::string& name)
{
    const std::string::size_type dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == name.size() - 1)
        return "";

    std::string ext = name.substr(dot + 1);
    for (char& c : ext)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    return ext;
}
