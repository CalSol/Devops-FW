#include "Mcp3201.h"
#include "Mcp4921.h"

#ifndef __SMU_ANALOG_STAGE_H__
#define __SMU_ANALOG_STAGE_H__

class SmuAnalogStage {
public:
  SmuAnalogStage(SPI& sharedSpi,
      Mcp4921& dacVolt, Mcp4921& dacCurrNeg, Mcp4921& dacCurrPos, DigitalOut& dacLdac,
      Mcp3201& adcVolt, Mcp3201& adcCurr, DigitalOut& enableSource, DigitalOut& enableSink):
      sharedSpi_(sharedSpi),
      dacVolt_(dacVolt), dacCurrNeg_(dacCurrNeg), dacCurrPos_(dacCurrPos), dacLdac_(dacLdac),
      adcVolt_(adcVolt), adcCurr_(adcCurr), enableSource_(enableSource), enableSink_(enableSink),
      state_(kDisabled) {
    timer_.start();
    enableSource_ = 0;
    enableSink_ = 0;
  }

  // Reads the voltage ADC, returning millivolts (and optionally also the raw ADC counts)
  int32_t readVoltageMv(uint16_t* rawAdcOut = NULL) {
    sharedSpi_.frequency(100000);  // TODO refactor into Mcp* classes
    uint16_t adcValue = adcVolt_.read_raw_u12();
    if (rawAdcOut != NULL) {
      *rawAdcOut = adcValue;
    }
    return (adcValue - voltageAdcIntercept_) * kCalibrationDenominator * 1000 / voltageAdcSlope_;
  }

  // Reads the current ADC, returning millivolts (and optionally also the raw ADC counts)
  int32_t readCurrentMa(uint16_t* rawAdcOut = NULL) {
    sharedSpi_.frequency(100000);
    uint16_t adcValue = adcCurr_.read_raw_u12();
    if (rawAdcOut != NULL) {
      *rawAdcOut = adcValue;
    }
    return (adcValue - currentAdcIntercept_) * kCalibrationDenominator * 1000 / currentAdcSlope_;
  }

  void setVoltageMv(int32_t setVoltage) {
    setVoltageDac(voltageToDac(setVoltage));
  }

  void setCurrentSourceMa(int32_t setCurrentSource) {
    setCurrentSourceDac(currentToDac(setCurrentSource));
  }

  void setCurrentSinkMa(int32_t setCurrentSink) {
    setCurrentSinkDac(currentToDac(setCurrentSink));
  }

  void setVoltageDac(uint16_t dacValue) {
    targetVoltageDac_ = dacValue;

    if (state_ == kEnabled) {  // only write DAC immediately if enabled
      writeVoltage(targetVoltageDac_);
    }
  }

  void setCurrentSourceDac(uint16_t dacValue) {
    targetCurrentSourceDac_ = dacValue;

    if (state_ == kEnabled) {  // only write DAC immediately if enabled
      writeCurrentSource(targetCurrentSourceDac_);
    }
  }

  void setCurrentSinkDac(uint16_t dacValue) {
    targetCurrentSinkDac_ = dacValue;

    if (state_ == kEnabled) {  // only write DAC immediately if enabled
      writeCurrentSink(targetCurrentSinkDac_);
    }
  }

  void enableDriver() {
    enableSource_ = 0;
    enableSink_ = 0;
    if (dacToVoltage(targetVoltageDac_) >= readVoltageMv()) {  // likely will be sourcing current
      dacVolt_.write_u16(65535);  // command lowest voltage
      startSourceDriver_ = true;
    } else {  // likely will be sinking current
      dacVolt_.write_u16(0);  // command highest voltage
      startSourceDriver_ = false;
    }
    writeCurrentSource(targetCurrentSourceDac_);  // also does LDAC
    writeCurrentSink(targetCurrentSinkDac_);
    timer_.reset();
    state_ = kResetIntegrator;
  }

  void disableDriver() {
    enableSource_ = 0;
    enableSink_ = 0;
    state_ = kDisabled;
  }

  void update() {
    switch (state_) {
      case SmuState::kDisabled:
        enableSource_ = 0;
        enableSink_ = 0;
        break;
      case SmuState::kResetIntegrator:
        enableSource_ = 0;
        enableSink_ = 0;
        if (timer_.read_ms() >= kIntegratorResetTimeMs) {
          writeVoltage(targetVoltageDac_);
          if (startSourceDriver_) {
            enableSource_ = 1;
          } else {
            enableSink_ = 1;
          }
          state_ = SmuState::kSingleEnable;
        }
        break;
      case SmuState::kSingleEnable:
        if (timer_.read_ms() >= kIntegratorResetTimeMs) {
          enableSource_ = 1;
          enableSink_ = 1;
          state_ = SmuState::kEnabled;
        }
        break;
      case SmuState::kEnabled:
        enableSource_ = 1;
        enableSink_ = 1;
        break;
    }
  }

  enum SmuState {
    kDisabled,  // both drivers off
    kResetIntegrator,  // both drivers off, rail the integrator
    kSingleEnable,  // enable a single driver to stabilize the integrator
    kEnabled,  // both drivers active
  };

  SmuState getState() {
    return state_;
  }


  // "Advanced API" that is one level of abstraction lower
  // but allows raw DAC/ADC counts for example for calibration
  uint16_t voltageToDac(int32_t voltageMv) {
    return (int64_t)voltageMv * voltageDacSlope_ / kCalibrationDenominator / 1000 + voltageDacIntercept_;
  }
  int32_t dacToVoltage(int32_t dacValue) {
    return (int64_t)(dacValue - voltageDacIntercept_) * kCalibrationDenominator * 1000 / voltageDacSlope_;
  }

  uint16_t currentToDac(int32_t currentMa) {
    return (int64_t)currentMa * currentDacSlope_ / kCalibrationDenominator / 1000 + currentDacIntercept_;
  }
  int32_t dacToCurrent(int32_t dacValue) {
    return (int64_t)(dacValue - currentDacIntercept_) * kCalibrationDenominator * 1000 / currentDacSlope_;
  }

  // Slope and intercept are adcBits = volts * slope + intercept
  void setVoltageAdcCalibration(float slope, float intercept) {
    voltageAdcSlope_ = slope * 1000;
    voltageAdcIntercept_ = intercept;
  }
  void setCurrentAdcCalibration(float slope, float intercept) {
    currentAdcSlope_ = slope * 1000;
    currentAdcIntercept_ = intercept;
  }

  void setVoltageDacCalibration(float slope, float intercept) {
    voltageDacSlope_ = slope * 1000;
    voltageDacIntercept_ = intercept;
  }
  void setCurrentDacCalibration(float slope, float intercept) {
    currentDacSlope_ = slope * 1000;
    currentDacIntercept_ = intercept;
  }

protected:
  void writeVoltage(uint16_t dacValue) {
    sharedSpi_.frequency(1000000);
    dacVolt_.write_raw_u12(dacValue);
    dacLdac_ = 1;
    wait_us(1);
    dacLdac_ = 0;
  }

  void writeCurrentSource(uint16_t dacValue) {
    sharedSpi_.frequency(1000000);
    dacCurrPos_.write_raw_u12(dacValue);
    dacLdac_ = 1;
    wait_us(1);
    dacLdac_ = 0;
  }

  void writeCurrentSink(uint16_t dacValue) {
    sharedSpi_.frequency(1000000);
    dacCurrNeg_.write_raw_u12(dacValue);
    dacLdac_ = 1;
    wait_us(1);
    dacLdac_ = 0;
  }

  static const uint16_t kAdcCounts = 4095;
  static const uint16_t kDacCounts = 4095;

  // These are nominal (default, uncalibrated) parameters
  static const int32_t kVoltRatio = 22148;  // 1000x, actually ~22.148 Vout / Vmeas
  static const int32_t kAmpRatio = 10000;  // 1000x, actually 10 Aout / Vmeas
  static const int32_t kVrefMv = 3000;  // Vref voltage

  static const int32_t kCalibrationDenominator = 1000;
  int32_t voltageAdcSlope_ = (int64_t)kCalibrationDenominator * kAdcCounts * 1000 / kVrefMv * 1000 / kVoltRatio;
  int32_t voltageAdcIntercept_ = 4095 / 2;

  int32_t voltageDacSlope_ = -voltageAdcSlope_;
  int32_t voltageDacIntercept_ = voltageAdcIntercept_;

  int32_t currentAdcSlope_ = (int64_t)kCalibrationDenominator * kDacCounts * 1000 / kVrefMv * 1000 / kAmpRatio;
  int32_t currentAdcIntercept_ = 4095 / 2;

  int32_t currentDacSlope_ = -currentAdcSlope_;
  int32_t currentDacIntercept_ = currentAdcIntercept_;


  static const uint16_t kIntegratorResetTimeMs = 10;  // time to reset the integrator

  uint16_t targetVoltageDac_ = voltageToDac(0);
  uint16_t targetCurrentSourceDac_ = currentToDac(100), targetCurrentSinkDac_ = currentToDac(-100);

  SPI &sharedSpi_;

  Mcp4921 &dacVolt_, &dacCurrNeg_, &dacCurrPos_;
  DigitalOut &dacLdac_;

  Mcp3201 &adcVolt_, &adcCurr_;

  DigitalOut &enableSource_, &enableSink_;

  SmuState state_;
  bool startSourceDriver_;  // first driver to be enabled is source
  Timer timer_;
};

#endif  // __SMU_ANALOG_STAGE_H__
