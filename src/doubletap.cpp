#include "doubletap.h"

bool DoubleTapDetector::feed(Input input, qint64 msec)
{
    switch (input) {
    case Input::ControlDown:
        if (pressed) return false; // Key repeat while held.
        pressed = true;
        downAt = msec;
        if (fire == Fire::OnSecondPress && lastTapAt >= 0 && msec - lastTapAt <= maxGapMs) {
            lastTapAt = -1;
            firedOnPress = true;
            return true;
        }
        return false;
    case Input::ControlUp: {
        if (!pressed) return false;
        pressed = false;
        if (firedOnPress) { firedOnPress = false; return false; }
        if (msec - downAt > maxTapMs) { lastTapAt = -1; return false; }
        const bool second = lastTapAt >= 0 && downAt - lastTapAt <= maxGapMs;
        lastTapAt = second ? -1 : msec;
        return second;
    }
    case Input::Other:
        pressed = false; // The pending release belongs to a chord, not a tap.
        firedOnPress = false;
        lastTapAt = -1;
        return false;
    }
    return false;
}
