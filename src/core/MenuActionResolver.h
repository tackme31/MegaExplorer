#pragma once
#include "MenuAction.h"

#include <vector>

// Pure resolution logic for "which right-click-menu actions apply here" --
// Qt-free so it's testable without any QML/QObject wiring (MegaExplorerTests
// already links MegaExplorerCore).
//
// This owns two of the three things a menu needs: which actions a site offers,
// and whether each applies to the current state. The third -- display text,
// greying, and what the action actually does -- lives in QML
// (qml/ActionCatalog.qml), because every execution target
// (downloadController, tabsController, quickAccessModel, each view's inline
// rename field and dialogs) is a QML-side object src/core can't see. Same
// C++-supplies-structure / QML-supplies-wording split as
// NotificationController and ToastStack.qml.

// Whether a single spec applies in ctx. An empty selection always returns
// false regardless of spec, so {Any, Any} doesn't leak through when nothing is
// selected (e.g. right after clearSelection()); the fixed-target sites are
// unaffected since folderTargetContext() never synthesizes an empty one.
bool menuActionApplies(const MenuActionSpec& spec, const MenuContext& ctx);

// The app's full action vocabulary, in menu display order. Global rather than
// per-site on purpose -- see MenuSite's comment.
const std::vector<MenuActionSpec>& defaultMenuActions();

// Filters specs down to the ones that apply in ctx, preserving specs' order.
// Takes specs explicitly (defaulting to defaultMenuActions()) so
// MenuActionResolverTest can exercise ActionTarget/ActionArity/MenuSite
// combinations that don't exist in the real table yet, via a synthetic list.
std::vector<MenuAction>
resolveMenuActions(const MenuContext& ctx,
                   const std::vector<MenuActionSpec>& specs = defaultMenuActions());

// Context for a site whose target is always exactly one folder
// (FolderBackground: the folder being shown; FolderRow: the row clicked).
// Synthesizing {0 files, 1 folder} is what lets those sites reuse the
// selection-shaped target/arity axes unchanged.
MenuContext folderTargetContext(MenuSite site);

// Stable string ID handed to QML (e.g. "download"), since MenuAction lives
// in Qt-free src/core and Q_ENUM would require a Q_NAMESPACE wrapper in
// src/qml just for this -- more machinery than the context-property-based
// controllers here otherwise need. Unknown/future actions return "".
const char* menuActionId(MenuAction action);
