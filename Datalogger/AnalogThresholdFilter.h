#ifndef _ANALOG_THRESHOLD_FILTER_H_
#define _ANALOG_THRESHOLD_FILTER_H_

#include "mbed.h"
#include "LongTimer.h"

class AnalogThresholdFilter {
public:
  enum State {
    kLow = 0,
    kHigh,
    kRising,
    kFalling
  };

  AnalogThresholdFilter(Timer& timebase, bool initialValue,
      uint32_t risingThreshold, uint32_t fallingThreshold,
      uint32_t filterDelayUs) :
      timebase_(timebase), lastValue_(initialValue), filteredValue_(initialValue),
      risingThreshold_(risingThreshold), fallingThreshold_(fallingThreshold),
      filterRiseUs_(filterDelayUs), filterFallUs_(filterDelayUs),
      filterUpdateTime_(0) {
  }
  AnalogThresholdFilter(Timer& timebase, bool initialValue,
      uint32_t risingThreshold, uint32_t fallingThreshold,
      uint32_t filterRiseUs, uint32_t filterFallUs) :
      timebase_(timebase), lastValue_(initialValue), filteredValue_(initialValue),
      risingThreshold_(risingThreshold), fallingThreshold_(fallingThreshold),
      filterRiseUs_(filterRiseUs), filterFallUs_(filterFallUs),
      filterUpdateTime_(0) {
  }

  /**
   * Read the filtered value without updating internal state.
   */
  bool read() {
    return filteredValue_;
  }

  /**
   * Updates the filter with the latest value, returning the current state.
   * The filter exhibits hysteresis if the thresholds are appropriately specified.
   */
  State update(uint32_t dataValue) {
    if ((dataValue >= risingThreshold_ && filteredValue_ == false)
        || (dataValue <= fallingThreshold_ && filteredValue_ == true)) {
      bool digitalValue = dataValue >= risingThreshold_;
      if (digitalValue == lastValue_) {
        if (LongTimer::timePast(timebase_.read_us(), filterUpdateTime_)) {  // past filter time
          filteredValue_ = digitalValue;
          if (digitalValue == true) {
            return State::kRising;
          } else {
            return State::kFalling;
          }
        } else {
          // no update needed
        }
      } else {  // first detection of changed data, set filter deadline
        lastValue_ = digitalValue;
        if (digitalValue == true) {  // rising edge
          filterUpdateTime_ = timebase_.read_us() + filterRiseUs_;
        } else {
          filterUpdateTime_ = timebase_.read_us() + filterFallUs_;
        }
      }
    } else {
      // do nothing, current value will not trigger a rising or falling edge
      lastValue_ = filteredValue_;  // reset any filtered-out data glitching
    }
    if (filteredValue_) {
      return State::kHigh;
    } else {
      return State::kLow;
    }
  }

protected:
  Timer& timebase_;
  const uint32_t risingThreshold_;
  const uint32_t fallingThreshold_;
  const uint32_t filterRiseUs_;
  const uint32_t filterFallUs_;

  uint32_t filterUpdateTime_;  // time at which filteredValue becomes lastValue (if different)
  bool lastValue_;  // last observed value
  bool filteredValue_;  // current filter output
};

#endif
