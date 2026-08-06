#pragma once
#include <QCoreApplication>

// Shared by every test that exercises an src/qml class whose async results
// arrive through a queued invoke onto the GUI thread (see
// src/qml/GuiThread.h): the queued event still needs
// real QCoreApplication/event-loop plumbing to be delivered to, even though
// MockMegaClient's InvokeArgument action fires the SDK callback synchronously.
//
// Qt permits exactly one QCoreApplication per process, so this must be the
// only instance in the test binary -- do not copy it into a second
// translation unit, or the second construction aborts with "There should be
// only one application object".
//
// Function-local static rather than a file-scope global: it must not be
// constructed during static initialization (Qt wants a live argc/argv pair and
// an otherwise-initialized process), and argc/argv have to outlive it -- hence
// the statics inside. Being a static inside an inline function, every
// translation unit including this header shares the same one.
inline QCoreApplication& testApp()
{
    static int argc = 1;
    static char arg0[] = "MegaExplorerTests";
    static char* argv[] = {arg0, nullptr};
    static QCoreApplication app(argc, argv);
    return app;
}

inline void flushQueuedEvents()
{
    QCoreApplication::processEvents();
}
