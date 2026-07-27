#pragma once
#include <string>

// Uppercased file extension (without the leading dot), the raw ingredient
// for the "kind" column's display text. Kept ASCII-only/pure on purpose:
// this codebase's convention (see NotificationController/ErrorToast.qml,
// docs/PROGRESS.md's Phase 6a notes) is "C++ passes structured fields, QML
// composes user-facing text" -- composing the final Japanese wording
// ("<EXT> ファイル" / "ファイル フォルダー" / "ファイル" alone) is left to
// FileTableView.qml, which also has the isFolder role this function doesn't
// need. Embedding Japanese literals directly in a C++ header/source instead
// hits an MSVC gotcha: without a UTF-8 BOM, non-ASCII bytes are interpreted
// against the build machine's codepage (932/Shift-JIS here), which can trip
// C4819 depending on the exact byte sequence -- avoided entirely by keeping
// this file ASCII-only.
//
// Returns an empty string if name has no extension, or ends in a bare "."
// with nothing after it (e.g. "archive."). ASCII-only uppercasing: a
// non-ASCII extension is left as-is.
std::string fileExtensionUppercased(const std::string& name);
