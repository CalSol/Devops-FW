#include <mbed.h>

#ifndef __BUTTON_GESTURE_H__
#define __BUTTON_GESTURE_H__

const uint16_t kClickDurationMs = 700;  // duration boundary between click and click-and-hold
const uint16_t kDebounceDurationMs = 50;  // duration to debounce edge - must be stable for this long to register change

/**
 * Button gesture detector (click / click-and-hold / long press) with debouncing.
 */
class ButtonGesture {
public:
  enum Gesture {
    kUp,  // repeatedly emitted when up
    kClickDown,  // emitted on down edge
    kDown,  // repeatedly emitted when held, when short enough to not be a long press
    kClickUp,  // emitted on up edge, if short enough to not be a long press
    kHeldTransition,  // emitted when held long enough to be a long press
    kHeld,  // repeatedly emitted when held past the long click duration
    kHeldUp,  // emitted on the up edge, if long enough to be a long press
  };

  ButtonGesture(DigitalIn& din) : din_(din) {
    debounceTimer_.start();
    pressTimer_.start();
  }

  Gesture update() {
    bool buttonState = din_;
    bool newDebounceState = debouncedState_;
    bool newLongPress = isLongPress_;
    if (buttonState != debouncedState_) {
      if (debounceTimer_.read_ms() > kDebounceDurationMs) {
        newDebounceState = buttonState;
      }
    } else {
      debounceTimer_.reset();
    }

    // Check for long press
    if (!debouncedState_ && !isLongPress_ && pressTimer_.read_ms() > kClickDurationMs) {
      newLongPress = true;
    }

    Gesture result;
    if (newDebounceState != debouncedState_) {  // edge
      if (!newDebounceState) {  // down edge
        pressTimer_.reset();
        newLongPress = false;
        result = Gesture::kClickDown;
      } else {  // up edge
        if (isLongPress_) {
          result = Gesture::kHeldUp;
        } else {
          result = Gesture::kClickUp;
        }
      }
    } else {  // holding
      if (!newDebounceState) {  // held down
        if (newLongPress && !isLongPress_) {
          result = Gesture::kHeldTransition;
        } else if (isLongPress_) {
          result = Gesture::kHeld;
        } else {
          result = Gesture::kDown;
        }
      } else {  // released up
        result = Gesture::kUp;
      }
    }
    debouncedState_ = newDebounceState;
    isLongPress_ = newLongPress;
    return result;
  }

protected:
  DigitalIn& din_;
  
  bool debouncedState_ = true;  // inverted logic, true is up
  bool isLongPress_ = false;

  Timer debounceTimer_;
  Timer pressTimer_;
};

#endif  // __BUTTON_GESTURE_H__
