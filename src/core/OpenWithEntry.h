#pragma once
#include <string>
#include <string_view>

// One user-registered "Open with" program: a display name, the extensions it is
// offered for, and the command line that starts it
// (docs/investigations/STUDY_OPEN_WITH.md 3-3).

struct OpenWithEntry
{
    std::string name;
    // Kept exactly as typed, because the settings list shows it back to the user;
    // normalizing happens in matches() instead. Empty matches every file.
    std::string extensions;
    std::string commandLine;

    // Field-by-field, as elsewhere.
    bool operator==(const OpenWithEntry& other) const
    {
        return name == other.name && extensions == other.extensions &&
               commandLine == other.commandLine;
    }
};

// Whether fileName's extension is one of those listed. Separators are commas,
// semicolons and whitespace, a leading "." is optional, and the comparison is
// ASCII case-insensitive. An empty list matches anything; a file with no
// extension matches only an empty list.
bool openWithExtensionMatches(const std::string& extensions, const std::string& fileName);

// The menu ID for the n-th registered program. The action vocabulary in
// MenuAction.h is fixed-size and this list is not, so the index rides along in
// the ID rather than becoming N enum values (STUDY_OPEN_WITH.md 3-3-3).
std::string openWithCustomActionId(int index);

// That ID's index, or -1 when actionId is not one. Rejects anything but decimal
// digits after the colon, so a hand-typed ID cannot index the list by accident.
int openWithCustomIndex(std::string_view actionId);
