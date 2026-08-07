#pragma once
#include <QObject>
#include <QTimer>

// The "this tab has something in flight" counter behind
// FolderNavigationController's busy property, plus the delay before a spinner shows.
//
// Owns a QTimer, so it has to die on the GUI thread: created through makeGuiOwned,
// and a shared_ptr rather than a member because both of a tab's controllers write it.
//
// Three invariants:
//  - one begin/end pair per SDK call, so a fan-out of N calls is N pairs. The end
//    goes at the very top of the callback, before any branching -- four outcomes
//    would otherwise be four chances to leak the count.
//  - visible() is not "count > 0": it turns true only once the delay timer fires, so
//    an operation finishing inside the delay shows no spinner at all.
//  - abandonAll() breaks the pairing on purpose, which is why end() clamps at zero.
class BusyState : public QObject
{
    Q_OBJECT

public:
    explicit BusyState(QObject* parent = nullptr);

    bool visible() const;

    void begin();
    void end();

    // For logout: abandons the count instead of waiting operations out, since their
    // callbacks will find nothing to refresh and the spinner would keep turning on a
    // signed-out window. They still reach end(), which is what its clamp is for.
    void abandonAll();

signals:
    // visible() flipped. Relayed, not exposed: QML sees one bool on the owning
    // controller.
    void changed();

private:
    int mCount = 0;
    bool mVisible = false;
    QTimer mDelayTimer;
};
