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

  int32_t readVoltageMv() {
    sharedSpi_.frequency(100000);  // TODO refactor into Mcp* classes
    uint16_t adc = adcVolt_.read_u16();
    return ((int64_t)adc - kAdcCenter) * kVrefMv * kVoltRatio / 1000 / 65535;  // in mV
  }
  int32_t readCurrentMa() {
    sharedSpi_.frequency(100000);
    uint16_t adc = adcCurr_.read_u16();
    return ((int64_t)adc - kAdcCenter) * kVrefMv * kAmpRatio / 1000 / 65535;  // in mA
  }

  void setVoltageMv(int32_t setVoltage) {
    targetVoltage_ = setVoltage;

    if (state_ == kEnabled) {  // only write DAC immediately if enabled
      writeVoltageMv(targetVoltage_);
    }
  }

  void setCurrentSinkMa(int32_t setCurrentSink) {
    targetCurrentSink_ = setCurrentSink;

    if (state_ == kEnabled) {  // only write DAC immediately if enabled
      writeCurrentSinkMa(targetCurrentSink_);
    }
  }

  void setCurrentSourceMa(int32_t setCurrentSource) {
    targetCurrentSource_ = setCurrentSource;

    if (state_ == kEnabled) {  // only write DAC immediately if enabled
      writeCurrentSourceMa(targetCurrentSource_);
    }
  }

  void enableDriver() {
    enableSource_ = 0;
    enableSink_ = 0;
    if (targetVoltage_ >= readVoltageMv()) {  // likely will be sourcing current
      dacVolt_.write_u16(65535);  // command lowest voltage
      startSourceDriver_ = true;
    } else {  // likely will be sinking current
      dacVolt_.write_u16(0);  // command highest voltage
      startSourceDriver_ = false;
    }
    writeCurrentSourceMa(targetCurrentSource_);  // also does LDAC
    writeCurrentSinkMa(targetCurrentSink_);
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
          writeVoltageMv(targetVoltage_);
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

protected:
  void writeVoltageMv(int32_t setVoltage) {
    sharedSpi_.frequency(1000000);
    int32_t setVOffset = (int64_t)setVoltage * 65535 * 1000 / kVoltRatio / kVrefMv;
    uint16_t dac = kDacCenter - setVOffset;
    dacVolt_.write_u16(dac);
    dacLdac_ = 1;
    wait_us(1);
    dacLdac_ = 0;
  }

  void writeCurrentSinkMa(int32_t setCurrentSink) {
    sharedSpi_.frequency(1000000);
    int32_t setISnkOffset = (int64_t)setCurrentSink * 65535 * 1000 / kAmpRatio / kVrefMv;
    uint16_t dac = kDacCenter - setISnkOffset;
    dacCurrNeg_.write_u16(dac);
    dacLdac_ = 1;
    wait_us(1);
    dacLdac_ = 0;
  }

  void writeCurrentSourceMa(int32_t setCurrentSource) {
    sharedSpi_.frequency(1000000);
    int32_t setISrcOffset = (int64_t)setCurrentSource * 65535 * 1000 / kAmpRatio / kVrefMv;
    uint16_t dac = kDacCenter - setISrcOffset;
    dacCurrPos_.write_u16(dac);
    dacLdac_ = 1;
    wait_us(1);
    dacLdac_ = 0;
  }

  int32_t targetVoltage_ = 0, targetCurrentSink_ = -100, targetCurrentSource_ = 100;

  SPI &sharedSpi_;

  Mcp4921 &dacVolt_, &dacCurrNeg_, &dacCurrPos_;
  DigitalOut &dacLdac_;

  Mcp3201 &adcVolt_, &adcCurr_;

  DigitalOut &enableSource_, &enableSink_;

  SmuState state_;
  bool startSourceDriver_;  // first driver to be enabled is source
  Timer timer_;

  // TODO also needs a linear calibration constant?
  static const int32_t kVoltRatio = 22148;  // 1000x, actually ~22.148 Vout / Vmeas
  static const int32_t kAmpRatio = 10000;  // 1000x, actually 10 Aout / Vmeas
  static const int32_t kVrefMv = 3000;  // Vref voltage

  static const uint16_t kAdcCenter = 2042 * 65535 / 4095;  // Measured center value of the ADC
  static const uint16_t kDacCenter = 2048 * 65535 / 4095;  // Empirically derived center value of the DAC

  static const uint16_t kIntegratorResetTimeMs = 10;  // time to reset the integrator
};

#endif  // __SMU_ANALOG_STAGE_H__
