#pragma once
#include <QGuiApplication>
#include <QObject>

#include <QtQml/qqmlregistration.h>

// The modifier keys held *right now*, for QML that cannot wait for an event.
//
// Drag & drop is the only caller and the reason this exists: QML's DragEvent carries
// no modifier state, and an internal drag delivers events only while the dragged
// item moves -- so Ctrl pressed with the pointer sitting still over a drop target
// reaches nothing. Keys.onPressed is focus-dependent, which a spring-loaded tab
// switch breaks mid-gesture.
//
// queryKeyboardModifiers(), not keyboardModifiers(): the latter answers from the
// last event Qt delivered, which is exactly the state this exists to avoid.
class KeyboardState : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    // Qt::KeyboardModifiers as an int. Deliberately raw rather than a
    // controlPressed() convenience, so the caller decides what a combination means
    // (a copy drag is Ctrl *without* Shift).
    Q_INVOKABLE int modifiers() const
    {
        return static_cast<int>(QGuiApplication::queryKeyboardModifiers().toInt());
    }
};
