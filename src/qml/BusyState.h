#pragma once
#include <QObject>
#include <QTimer>

// The "this tab has something in flight" counter behind
// FolderNavigationController's busy property, with the delay before a spinner
// is shown (REFACTOR_PLANS.md's R5-3).
//
// Owns a QTimer, so hold it as a value member of a GUI-thread-owned object.
// Handing a shared_ptr to it into an SDK callback would let ~QTimer run on the
// SDK thread -- the hazard GuiThread.h's makeGuiOwned answers for the
// controllers.
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
