#include <mbed.h>

#ifndef __BUTTON_GESTURE_H__
#define __BUTTON_GESTURE_H__

/**
 * Button gesture detector (click / click-and-hold / long press) with debouncing.
 */
class ButtonGesture {
public:
  enum Gesture {
    kUp,  // repeatedly emitted when not pressed (up)
    kClickPress,  // emitted on press edge (up to down)
    kDown,  // repeatedly emitted when pressed, but not held long enough to be a long press
    kClickRelease,  // emitted on release edge (down to up), when not held long enough to be a long press
    kHoldTransition,  // emitted once when held long enough to be a long press
    kHold,  // repeatedly emitted when held long enough to be a long press
    kHoldRepeat,  // emitted periodically (every holdRepeatMs) when held long enough to e a long press
    kHoldRelease,  // emitted on release edge (down to up), when held long enough to be a long press
  };

  ButtonGesture(DigitalIn& din, int clickTimeMs=700, int holdRepeatMs=100, int debounceTimeMs=50) :
      din_(din), clickTimeMs_(clickTimeMs), holdRepeatMs_(holdRepeatMs), debounceTimeMs_(debounceTimeMs) {
    debounceTimer_.start();
    pressTimer_.start();
  }

  Gesture update() {
    bool buttonState = din_;
    bool newDebounceState = debouncedState_;
    if (buttonState != debouncedState_) {
      if (debounceTimer_.read_ms() > debounceTimeMs_) {
        newDebounceState = buttonState;
      }
    } else {
      debounceTimer_.reset();
    }

    Gesture result;
    if (newDebounceState != debouncedState_) {  // edge
      if (!newDebounceState) {  // down edge
        pressTimer_.reset();
        result = Gesture::kClickPress;
      } else {  // up edge
        if (isLongPress_) {
          result = Gesture::kHoldRelease;
        } else {
          result = Gesture::kClickRelease;
        }
        isLongPress_ = false;
      }
    } else {  // holding
      if (!newDebounceState) {  // held down
        if (!isLongPress_ && pressTimer_.read_ms() > clickTimeMs_) {  // new long press
          isLongPress_ = true;
          pressTimer_.reset();
          result = Gesture::kHoldTransition;
        } else if (isLongPress_) {  // holding previous long press
          if (pressTimer_.read_ms() > holdRepeatMs_) {
            pressTimer_.reset();
            result = Gesture::kHoldRepeat;
          } else {
            result = Gesture::kHold;
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
  
  int clickTimeMs_;  // duration boundary between click and click-and-hold
  int holdRepeatMs_;  // duration between generating hold repeat events
  int debounceTimeMs_;  // duration to debounce edge - must be stable for this long to register change

  Timer debounceTimer_;
  Timer pressTimer_;

  bool debouncedState_ = true;  // inverted logic, true is up
  bool isLongPress_ = false;
};

#endif  // __BUTTON_GESTURE_H__
