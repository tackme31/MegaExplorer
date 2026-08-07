#pragma once
#include <QObject>
#include <QTimer>

// The "this tab has something in flight" counter behind
// FolderNavigationController's busy property, with the delay before a spinner
// is shown (REFACTOR_PLANS.md's R5-3).
//
// Owns a QTimer, so it has to die on the GUI thread. Created through
// GuiThread.h's makeGuiOwned and shared by the two controllers of one tab --
// navigation and mutations (R5-1) -- which is why it is a shared_ptr rather
// than a value member of either. Both holders are makeGuiOwned themselves, so
// refcount zero already lands on the GUI thread; makeGuiOwned here keeps that a
// local fact instead of one derived from the holders.
//
// Three invariants, previously spread over FolderNavigationController:
//  - begin/end pair per SDK call, so a bulk fan-out of N calls is N pairs. The
//    end goes at the very top of the callback, before any of its own
//    branching -- createFolder's four outcomes would otherwise be four chances
//    to leak the count.
//  - visible() is not "count > 0". It only turns true once the delay timer has
//    fired, so an operation that finishes inside the delay never shows a
//    spinner at all, which is the point.
//  - abandonAll() breaks the pairing on purpose, which is why end() clamps at
//    zero.
class BusyState : public QObject
{
    Q_OBJECT

public:
    explicit BusyState(QObject* parent = nullptr);

    bool visible() const;

    void begin();
    void end();

    // Abandons the count with operations still in flight, rather than waiting
    // them out: for logout, where their callbacks will find nothing left to
    // refresh and a spinner would otherwise keep turning on a signed-out
    // window. Those callbacks still reach end(), which is what its clamp is
    // for.
    void abandonAll();

signals:
    // visible() flipped. Relayed rather than exposed directly: QML sees this
    // as one bool on the controller that owns it.
    void changed();

private:
    int mCount = 0;
    bool mVisible = false;
    QTimer mDelayTimer;
};
