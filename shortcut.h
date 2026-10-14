#pragma once

#include <QKeySequence>
#include <QLineEdit>
#include <array>

struct Shortcut {
    QKeySequence key;
    Qt::MouseButtons buttons{};
    Qt::KeyboardModifiers modifiers{};
    Shortcut(QKeySequence key = {}) : key(key) {}
    Shortcut(Qt::MouseButton button) : buttons(button) {}
    Shortcut(Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = {}) : buttons(buttons), modifiers(modifiers) {}
    bool isEmpty() const { return key.isEmpty() && !buttons; }
    QString toString(QKeySequence::SequenceFormat format = QKeySequence::NativeText) const;
    static Shortcut fromString(const QString &text);
    static Shortcut fromKeyEvent(const QKeyEvent *event);
    bool operator==(const Shortcut &other) const { return key == other.key && buttons == other.buttons && modifiers == other.modifiers; }
};

// A side-button gesture completes on release, so a two-button chord wins over its prefix.
struct MouseGesture {
    struct Result { bool consume = false; int action = -1; Qt::MouseButtons replay{}; Qt::KeyboardModifiers modifiers{}; };
    Qt::MouseButtons held{}, seen{};
    Qt::KeyboardModifiers modifiers{};
    bool owned = false;
    Result event(Qt::MouseButton button, bool down, Qt::KeyboardModifiers mods, const std::array<Shortcut, 3> &bindings);
};

class ShortcutEdit : public QLineEdit {
public:
    explicit ShortcutEdit(QWidget *parent = nullptr);
    const Shortcut &binding() const { return current; }
    void setBinding(const Shortcut &binding);
protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    Shortcut current;
    Qt::MouseButtons held{}, seen{};
    Qt::KeyboardModifiers mouseModifiers{};
};
