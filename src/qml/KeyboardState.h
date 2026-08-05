#pragma once
#include <QGuiApplication>
#include <QObject>

#include <QtQml/qqmlregistration.h>

// The modifier keys held *right now*, for QML code that cannot wait for an
// event to tell it.
//
// Drag & drop is the only caller and the reason this exists. QML's DragEvent
// carries no modifier state at all, and an internal Qt Quick drag delivers
// events only when the dragged item moves -- so a Ctrl pressed while the
// pointer sits still over a drop target (which is exactly how "drag there,
// then decide to copy" feels) reaches nothing. Keys.onPressed on the views was
// the alternative and is focus-dependent, which a spring-loaded tab switch
// (Phase 22b) breaks mid-gesture.
//
// queryKeyboardModifiers(), not keyboardModifiers(): the latter answers from
// the last event Qt delivered, which is the state this class was written to
// avoid reading.
class KeyboardState : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    // Returns Qt::KeyboardModifiers as an int; QML compares it against
    // Qt.ControlModifier and friends. Deliberately raw rather than a
    // controlPressed() convenience, so the caller decides what combination
    // means what (a copy drag is Ctrl *without* Shift).
    Q_INVOKABLE int modifiers() const
    {
        return static_cast<int>(QGuiApplication::queryKeyboardModifiers().toInt());
    }
};
