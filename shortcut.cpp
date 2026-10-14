#include "shortcut.h"
#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStyle>

static const auto sideButtons = Qt::BackButton | Qt::ForwardButton;

QString Shortcut::toString(QKeySequence::SequenceFormat format) const
{
    if (!buttons) return key.toString(format);
    QString prefix = QKeySequence(QKeyCombination(modifiers, Qt::Key_F1)).toString(format);
    prefix.chop(2);
    QStringList names;
    if (buttons & Qt::BackButton) names << "Mouse4";
    if (buttons & Qt::ForwardButton) names << "Mouse5";
    return prefix + names.join('+');
}

Shortcut Shortcut::fromString(const QString &text)
{
    if (!text.contains("Mouse")) return QKeySequence(text, QKeySequence::PortableText);
    Shortcut result;
    for (const auto &part : text.split('+')) {
        if (part == "Mouse4") result.buttons |= Qt::BackButton;
        else if (part == "Mouse5") result.buttons |= Qt::ForwardButton;
        else if (part == "Ctrl") result.modifiers |= Qt::ControlModifier;
        else if (part == "Meta") result.modifiers |= Qt::MetaModifier;
        else if (part == "Alt") result.modifiers |= Qt::AltModifier;
        else if (part == "Shift") result.modifiers |= Qt::ShiftModifier;
        else return QKeySequence(Qt::Key_unknown);
    }
    if (result.toString(QKeySequence::PortableText) != text) return QKeySequence(Qt::Key_unknown);
    return result;
}

MouseGesture::Result MouseGesture::event(Qt::MouseButton button, bool down, Qt::KeyboardModifiers mods,
                                       const std::array<Shortcut, 3> &bindings)
{
    if (!(button & sideButtons)) return {};
    if (down) {
        if (!held) {
            modifiers = mods; seen = {}; owned = false;
            for (const auto &binding : bindings)
                if ((binding.buttons & button) && binding.modifiers == mods) owned = true;
        }
        held |= button; seen |= button;
        return {owned};
    }
    if (!(held & button)) return {};
    held &= ~Qt::MouseButtons(button);
    Result result{owned};
    if (held) return result;
    if (owned) {
        for (int i = 0; i < 3; ++i)
            if (bindings[i].buttons == seen && bindings[i].modifiers == modifiers) { result.action = i; break; }
        if (result.action < 0) { result.replay = seen; result.modifiers = modifiers; }
    }
    *this = {};
    return result;
}

ShortcutEdit::ShortcutEdit(QWidget *parent) : QLineEdit(parent)
{
    setObjectName("shortcutEdit"); setReadOnly(true); setPlaceholderText("Press keys or side buttons");
    auto *clear = addAction(style()->standardIcon(QStyle::SP_DialogCloseButton), QLineEdit::TrailingPosition);
    clear->setToolTip("Clear shortcut");
    connect(clear, &QAction::triggered, this, [this] { setBinding({}); });
}

void ShortcutEdit::setBinding(const Shortcut &binding)
{
    current = binding; setText(current.toString());
}

bool ShortcutEdit::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride && hasFocus()) { event->accept(); return true; }
    return QLineEdit::event(event);
}

void ShortcutEdit::keyPressEvent(QKeyEvent *event)
{
    const auto key = event->key();
    if (event->isAutoRepeat()) return;
    if (key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter) { clearFocus(); return; }
    if (key == Qt::Key_Tab || key == Qt::Key_Backtab) { QLineEdit::keyPressEvent(event); return; }
    if (key == Qt::Key_Control || key == Qt::Key_Meta || key == Qt::Key_Alt || key == Qt::Key_Shift) return;
    if ((key == Qt::Key_Backspace || key == Qt::Key_Delete) && !event->modifiers()) setBinding({});
    else if (!held) setBinding(Shortcut::fromKeyEvent(event));
    event->accept();
}

void ShortcutEdit::keyReleaseEvent(QKeyEvent *event) { event->accept(); }

void ShortcutEdit::focusInEvent(QFocusEvent *event)
{
    QLineEdit::focusInEvent(event); qApp->installEventFilter(this);
}

void ShortcutEdit::focusOutEvent(QFocusEvent *event)
{
    qApp->removeEventFilter(this); held = {}; seen = {}; setText(current.toString());
    QLineEdit::focusOutEvent(event);
}

bool ShortcutEdit::eventFilter(QObject *, QEvent *event)
{
    if (!hasFocus() || !isActiveWindow()) return false;
    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonRelease) return false;
    auto *mouse = static_cast<QMouseEvent *>(event);
    if (!(mouse->button() & sideButtons)) return false;
    if (event->type() == QEvent::MouseButtonPress) {
        if (!held) { seen = {}; mouseModifiers = mouse->modifiers(); }
        held |= mouse->button(); seen |= mouse->button();
        setText(Shortcut(seen, mouseModifiers).toString());
    } else if (held & mouse->button()) {
        held &= ~Qt::MouseButtons(mouse->button());
        if (!held) { setBinding(Shortcut(seen, mouseModifiers)); seen = {}; }
    }
    return true;
}
