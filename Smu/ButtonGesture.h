#include <mbed.h>

#ifndef __BUTTON_GESTURE_H__
#define __BUTTON_GESTURE_H__

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
    kHeldRepeat,  // emitted periodically when held down
    kHeldUp,  // emitted on the up edge, if long enough to be a long press
  };

  ButtonGesture(DigitalIn& din) : din_(din) {
    debounceTimer_.start();
    pressTimer_.start();
  }

  Gesture update() {
    bool buttonState = din_;
    bool newDebounceState = debouncedState_;
    if (buttonState != debouncedState_) {
      if (debounceTimer_.read_ms() > debounceDurationMs_) {
        newDebounceState = buttonState;
      }
    } else {
      debounceTimer_.reset();
    }

    Gesture result;
    if (newDebounceState != debouncedState_) {  // edge
      if (!newDebounceState) {  // down edge
        pressTimer_.reset();
        result = Gesture::kClickDown;
      } else {  // up edge
        if (isLongPress_) {
          result = Gesture::kHeldUp;
        } else {
          result = Gesture::kClickUp;
        }
        isLongPress_ = false;
      }
    } else {  // holding
      if (!newDebounceState) {  // held down
        if (!isLongPress_ && pressTimer_.read_ms() > clickDurationMs_) {  // new long press
          isLongPress_ = true;
          pressTimer_.reset();
          result = Gesture::kHeldTransition;
        } else if (isLongPress_) {  // holding previous long press
          if (pressTimer_.read_ms() > holdRepeatMs_) {
            pressTimer_.reset();
            result = Gesture::kHeldRepeat;
          } else {
            result = Gesture::kHeld;
          }
        } else {  // not long press
          result = Gesture::kDown;
        }
      } else {  // released up
        result = Gesture::kUp;
      }
    }
    debouncedState_ = newDebounceState;
    return result;
  }

protected:
  DigitalIn& din_;
  
  uint16_t clickDurationMs_ = 700;  // duration boundary between click and click-and-hold
  uint16_t holdRepeatMs_ = 100;  // duration between generating hold repeat events
  uint16_t debounceDurationMs_ = 50;  // duration to debounce edge - must be stable for this long to register change

  bool debouncedState_ = true;  // inverted logic, true is up
  bool isLongPress_ = false;

  Timer debounceTimer_;
  Timer pressTimer_;
};

#endif  // __BUTTON_GESTURE_H__
