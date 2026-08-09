#pragma once
#include "MenuAction.h"

#include <string_view>
#include <vector>

// Which right-click-menu actions apply here. Owns two of the three things a menu
// needs -- what a site offers, and whether each applies to the current state. The
// third (display text, greying, what the action does) lives in QML, because every
// execution target is a QML-side object src/core can't see.

// An empty selection always returns false, so {Any, Any} doesn't leak through when
// nothing is selected. The fixed-target sites are unaffected -- folderTargetContext()
// never synthesizes an empty one.
bool menuActionApplies(const MenuActionSpec& spec, const MenuContext& ctx);

// The app's full action vocabulary, in menu display order. Global rather than
// per-site on purpose -- see MenuSite's comment.
const std::vector<MenuActionSpec>& defaultMenuActions();

// Filters specs down to what applies in ctx, preserving order. Takes specs
// explicitly so tests can exercise combinations the real table doesn't have yet.
std::vector<MenuAction>
resolveMenuActions(const MenuContext& ctx,
                   const std::vector<MenuActionSpec>& specs = defaultMenuActions());

// Context for a site whose target is always exactly one folder. Synthesizing
// {0 files, 1 folder} lets those sites reuse the selection-shaped axes unchanged.
MenuContext folderTargetContext(MenuSite site, ViewKind kind);

// Whether the action with this stable ID applies in ctx. resolveMenuActions()'s
// counterpart for the keyboard, which has an ID but no menu to filter down.
// Unknown IDs are false.
bool menuActionAllowed(std::string_view actionId, const MenuContext& ctx);

// Stable string ID for QML, since MenuAction lives in Qt-free src/core and Q_ENUM
// would need a Q_NAMESPACE wrapper just for this. Unknown actions return "".
const char* menuActionId(MenuAction action);
