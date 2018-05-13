#ifndef _DIGITAL_FILTER_H_
#define _DIGITAL_FILTER_H_

#include "mbed.h"
#include "LongTimer.h"

class DigitalFilter {
public:
  enum Edge {
    kNone = 0,
    kRising,
    kFalling
  };

  DigitalFilter(Timer& timebase, bool initialValue, uint32_t filterDelayUs) :
      timebase_(timebase), lastValue_(initialValue), filteredValue_(initialValue), filterDelayUs_(filterDelayUs),
      filterUpdateTime_(0) {
  }

  /**
   * Updates the filter with the latest value, and returns true if there is a rising edge.
   */
  bool rising(bool dataValue) {
    return update(dataValue) == kRising;
  }

  /**
   * Updates the filter with the latest value, and returns true if there is a falling edge.
   */
  bool falling(bool dataValue) {
    return update(dataValue) == kFalling;
  }

  /**
   * Updates the filter with the latest value, returning any detected edges.
   */
  Edge update(bool dataValue) {
    if (dataValue != filteredValue_) {
      if (dataValue == lastValue_) {
        if (LongTimer::timePast(timebase_.read_us(), filterUpdateTime_)) {  // past filter time
          filteredValue_ = dataValue;
          if (dataValue == true) {
            return Edge::kRising;
          } else {
            return Edge::kFalling;
          }
        } else {
          // no update needed
        }
      } else {  // first detection of changed data, set filter deadline
        lastValue_ = dataValue;
        filterUpdateTime_ = timebase_.read_us() + filterDelayUs_;
      }
    } else {
      // do nothing, current data equivalent to the last filtered value
      lastValue_ = dataValue;  // reset lastValue
    }
    return Edge::kNone;
  }

protected:
  Timer& timebase_;
  const uint32_t filterDelayUs_;

  uint32_t filterUpdateTime_;  // time at which filteredValue becomes lastValue (if different)
  bool lastValue_;  // last observed value
  bool filteredValue_;  // current filter output
};

#endif
