#pragma once
#include <QCoreApplication>

// The process-wide QCoreApplication itself lives in TestMain.cpp; this header
// is only the pump.
//
// Needed by every test that exercises an src/qml class whose async results
// arrive through a queued invoke onto the GUI thread (see src/qml/GuiThread.h):
// the queued event still needs an event loop turn to be delivered in, even
// though MockMegaClient's InvokeArgument action fires the SDK callback
// synchronously.
inline void flushQueuedEvents()
{
    QCoreApplication::processEvents();
}
