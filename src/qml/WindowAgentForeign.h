#pragma once
#include <QtQml/qqmlregistration.h>
#include <QWKQuick/quickwindowagent.h>

// Registers QWK::QuickWindowAgent into *this* project's MegaExplorer QML
// module rather than using QWindowKit's own QWK::registerTypes(&engine).
// That helper is a runtime qmlRegisterType into a separate "QWindowKit" URI,
// which qmlcachegen/qmllint can't see at build time (the AOT compile would
// report an unresolved import). Registering it here instead makes
// `WindowAgent { }` usable from Main.qml with no extra import and no
// main.cpp change.
struct WindowAgentForeign
{
    Q_GADGET
    QML_NAMED_ELEMENT(WindowAgent)
    QML_FOREIGN(QWK::QuickWindowAgent)
};
