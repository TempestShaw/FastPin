#pragma once

#include <QtGlobal>
#include <functional>

inline constexpr char doubleControlSetting[] = "translate/doubleControl"; // Default: enabled.

// Recognizes two quick, clean taps of Control. Control used as a modifier
// (Ctrl+C, Ctrl-click) or held down never counts as a tap.
class DoubleTapDetector {
public:
    enum class Input { ControlDown, ControlUp, Other };
    static constexpr qint64 maxTapMs = 300;  // Press-to-release time for one tap.
    static constexpr qint64 maxGapMs = 400;  // First release to second press.
    bool feed(Input input, qint64 msec);
private:
    bool pressed = false;
    qint64 downAt = 0;
    qint64 lastTapAt = -1;
};

// Watches Control system-wide and calls triggered() on the main thread for each
// double tap. Returns a function that stops watching, or {} when unavailable
// (macOS needs Accessibility permission to observe other apps' key events).
std::function<void()> watchDoubleControl(std::function<void()> triggered);
