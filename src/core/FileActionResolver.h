#pragma once
#include "FileAction.h"

#include <vector>

// Pure resolution logic for "which context-menu actions apply to the current
// selection" -- Qt-free so it's testable without any QML/QObject wiring
// (MegaExplorerTests already links MegaExplorerCore). FileListModel is a
// thin delegator on top of this: selection -> SelectionSummary -> resolved
// FileAction list -> stable string IDs for QML.

// Whether a single spec applies to selection. An empty selection always
// returns false regardless of spec, so {Any, Any} doesn't leak through when
// nothing is selected (e.g. right after clearSelection()).
bool fileActionApplies(const FileActionSpec& spec, const SelectionSummary& selection);

// The app's full action table, in menu display order. Currently just
// Download for a files-only selection of any size; future actions (Rename,
// Delete, OpenInNewTab, ...) get appended here.
const std::vector<FileActionSpec>& defaultFileActions();

// Filters specs down to the ones that apply to selection, preserving specs'
// order. Takes specs explicitly (defaulting to defaultFileActions()) so
// FileActionResolverTest can exercise ActionTarget/ActionArity combinations
// (FoldersOnly, SingleOnly, MultiOnly) that don't exist in the real table
// yet, via a synthetic spec list.
std::vector<FileAction>
resolveFileActions(const SelectionSummary& selection,
                   const std::vector<FileActionSpec>& specs = defaultFileActions());

// Stable string ID handed to QML (e.g. "download"), since FileAction lives
// in Qt-free src/core and Q_ENUM would require a Q_NAMESPACE wrapper in
// src/qml just for this -- more machinery than the context-property-based
// controllers here otherwise need. Unknown/future actions return "".
const char* fileActionId(FileAction action);
