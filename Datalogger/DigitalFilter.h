#ifndef _DIGITAL_FILTER_H_
#define _DIGITAL_FILTER_H_

#include "mbed.h"
#include "LongTimer.h"

class DigitalFilter {
public:
  enum State {
    kLow = 0,
    kHigh,
    kRising,
    kFalling
  };

  DigitalFilter(Timer& timebase, bool initialValue, uint32_t filterDelayUs) :
      timebase_(timebase), lastValue_(initialValue), filteredValue_(initialValue),
      filterRiseUs_(filterDelayUs), filterFallUs_(filterDelayUs),
      filterUpdateTime_(0) {
  }
  DigitalFilter(Timer& timebase, bool initialValue, uint32_t filterRiseUs, uint32_t filterFallUs) :
      timebase_(timebase), lastValue_(initialValue), filteredValue_(initialValue),
      filterRiseUs_(filterRiseUs), filterFallUs_(filterFallUs),
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
   * Read the filtered value without updating internal state
   */
  bool read() {
    return filteredValue_;
  }

  /**
   * Updates the filter with the latest value, returning any detected edges.
   */
  State update(bool dataValue) {
    if (dataValue != filteredValue_) {
      if (dataValue == lastValue_) {
        if (LongTimer::timePast(timebase_.read_us(), filterUpdateTime_)) {  // past filter time
          filteredValue_ = dataValue;
          if (dataValue == true) {
            return State::kRising;
          } else {
            return State::kFalling;
          }
        } else {
          // no update needed
        }
      } else {  // first detection of changed data, set filter deadline
        lastValue_ = dataValue;
        if (dataValue == true) {  // rising edge
          filterUpdateTime_ = timebase_.read_us() + filterRiseUs_;
        } else {
          filterUpdateTime_ = timebase_.read_us() + filterFallUs_;
        }
      }
    } else {
      // do nothing, current data equivalent to the last filtered value
      lastValue_ = dataValue;  // reset lastValue
    }
    if (filteredValue_) {
      return State::kHigh;
    } else {
      return State::kLow;
    }
  }

protected:
  Timer& timebase_;
  const uint32_t filterRiseUs_;
  const uint32_t filterFallUs_;

  uint32_t filterUpdateTime_;  // time at which filteredValue becomes lastValue (if different)
  bool lastValue_;  // last observed value
  bool filteredValue_;  // current filter output
};

#endif
