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
      adcVolt_(adcVolt), adcCurr_(adcCurr), enableSource_(enableSource), enableSink_(enableSink) {
    timer_.start();
  }

  int32_t readVoltageMv() {
    uint16_t adc = adcVolt_.read_raw_u12();
    return ((int64_t)adc - kAdcCenter) * 3000 * kVoltRatio / 1000 / 4096;  // in mV    
  }
  int32_t readCurrentMa() {
    uint16_t adc = adcCurr_.read_raw_u12();
    return ((int64_t)adc - kAdcCenter) * 3000 * kAmpRatio / 1000 / 4096;  // in mA
  }

  void setVoltageMv(int32_t setVoltage) {
    int32_t setVOffset = (int64_t)setVoltage * 4096 * 1000 / kVoltRatio / 3000;
    uint16_t dac = kDacCenter - setVOffset;
    dacVolt_.write_raw_u12(dac);
    dacLdac_ = 1;
    dacLdac_ = 0;
  }
  void setCurrentSinkMa(int32_t setCurrentSink) {
    int32_t setISnkOffset = (int64_t)setCurrentSink * 4096 * 1000 / kAmpRatio / 3000;
    uint16_t dac = kDacCenter - setISnkOffset;
    dacCurrNeg_.write_raw_u12(dac);
    dacLdac_ = 1;
    dacLdac_ = 0;
  }
  void setCurrentSourceMa(int32_t setCurrentSource) {
    int32_t setISrcOffset = (int64_t)setCurrentSource * 4096 * 1000 / kAmpRatio / 3000;
    uint16_t dac = kDacCenter - setISrcOffset;
    dacCurrPos_.write_raw_u12(dac);
    dacLdac_ = 1;
    dacLdac_ = 0;
  }

  void enableDriver() {
    enableSource_ = 1;
    enableSink_ = 1;
  }
  void disableDriver() {
    enableSource_ = 0;
    enableSink_ = 0;
  }

  void update() {

  }

protected:
  SPI &sharedSpi_;

  Mcp4921 &dacVolt_, &dacCurrNeg_, &dacCurrPos_;
  DigitalOut &dacLdac_;

  Mcp3201 &adcVolt_, &adcCurr_;

  DigitalOut &enableSource_, &enableSink_;

  Timer timer_;

  // TODO also needs a linear calibration constant?
  static const int32_t kVoltRatio = 22148;  // 1000x, actually ~22.148 Vout / Vmeas
  static const int32_t kAmpRatio = 10000;  // 1000x, actually 10 Aout / Vmeas

  static const uint16_t kAdcCenter = 2042;  // Measured center value of the ADC
  static const uint16_t kDacCenter = 2048;  // Empirically derived center value of the DAC

  static const uint16_t kIntegratorResetTimeMs = 10;  // time to reset the integrator
};

#endif  // __SMU_ANALOG_STAGE_H__
