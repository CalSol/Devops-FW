#include <cstdio>

#include "USBSerial.h"

#define DEBUG_ENABLED
#include "debug.h"

#include "WDT.h"

#include "RgbActivityLed.h"
#include "DmaSerial.h"
#include "LongTimer.h"

#include "Mcp3201.h"
#include "Mcp4921.h"
#include "Fusb302.h"
#include "UsbPd.h"

#include "St7735sGraphics.h"
#include "DefaultFonts.h"
#include "Widget.h"

/*
 * Local peripheral definitions
 */
//
// System utilities
//
WDT Wdt(3 * 1000 * 1000);
Timer UsTimer;
LongTimer Timestamp(UsTimer);

DmaSerial<1024> swdConsole(P0_8, NC, 115200);  // TODO increase size when have more RAM

//
// Comms interfaces
//
// USBSerial UsbSerial;

//
// System
//
SPI SharedSpi(P0_3, P0_5, P0_6);  // mosi, miso, sclk

DigitalOut DacLdac(P0_0, 1);
DigitalOut DacCurrNegCs(P0_1, 1);
// Mcp4921 DacCurrNeg(SharedSpi, DigitalOut(P0_1, 1));
Mcp4921 DacCurrNeg(SharedSpi, DacCurrNegCs);
DigitalOut DacCurrPosCs(P0_2, 1);
Mcp4921 DacCurrPos(SharedSpi, DacCurrPosCs);
DigitalOut AdcVoltCs(P0_7, 1);
Mcp3201 AdcVolt(SharedSpi, AdcVoltCs);
DigitalOut AdcCurrCs(P0_9, 1);
Mcp3201 AdcCurr(SharedSpi, AdcCurrCs);
DigitalOut DacVoltCs(P0_18, 1);
Mcp4921 DacVolt(SharedSpi, DacVoltCs);

DigitalOut EnableHigh(P0_15);  // Current source transistor enable
DigitalOut EnableLow(P0_14);  // Current sink transistor enable

uint16_t kAdcCenter = 2042;  // Measured center value of the ADC
uint16_t kDacCenter = 2048;  // Empirically derived center value of the DAC
// TODO also needs a linear calibration constant?

DigitalIn PdInt(P0_17, PinMode::PullUp);
I2C SharedI2c(P0_23, P0_22);  // sda, scl
Fusb302 UsbPd(SharedI2c, PdInt);

//
// User interface
//
DigitalOut LcdCs(P0_13, 1);
DigitalOut LcdRs(P0_11);
DigitalOut LcdReset(P0_10, 0);

DigitalOut LedR(P0_29), LedG(P0_28), LedB(P0_27);
RgbActivityDigitalOut StatusLed(UsTimer, LedR, LedG, LedB, false);
TimerTicker LedStatusTicker(1 * 1000 * 1000, UsTimer);

DigitalIn SwitchL(P0_24, PinMode::PullUp);
DigitalIn SwitchR(P0_25, PinMode::PullUp);
DigitalIn SwitchC(P0_26, PinMode::PullUp);

//
// LCD and widgets
//
St7735sGraphics<160, 80, 1, 26> Lcd(SharedSpi, LcdCs, LcdRs, LcdReset);
TimerTicker LcdUpdateTicker(100 * 1000, UsTimer);

const uint8_t kContrastActive = 255;
const uint8_t kContrastStale = 191;

const uint8_t kContrastBackground = 191;

TextWidget widVersionData("USB PD SMU ", 0, Font5x7, kContrastActive);
TextWidget widBuildData("  " __DATE__ " " __TIME__, 0, Font5x7, kContrastBackground);

StaleNumericTextWidget widMeasV(0, 2, 100 * 1000, Font5x7, kContrastActive, kContrastStale, Font3x5, 1000, 2);
TextWidget widAdcVSep(" ", 0, Font5x7, kContrastStale);
NumericTextWidget widAdcV(0, 4, Font3x5, kContrastStale);
Widget* widMeasVContents[] = {&widMeasV, &widAdcVSep, &widAdcV};
HGridWidget<3> widMeasVGrid(widMeasVContents);
LabelFrameWidget widMeasVFrame(&widMeasVGrid, "MEAS V", Font3x5, kContrastBackground);

StaleNumericTextWidget widMeasI(0, 2, 100 * 1000, Font5x7, kContrastActive, kContrastStale, Font3x5, 1000, 2);
TextWidget widAdcISep(" ", 0, Font5x7, kContrastStale);
NumericTextWidget widAdcI(0, 4, Font3x5, kContrastStale);
Widget* widMeasIContents[] = {&widMeasI, &widAdcISep, &widAdcI};
HGridWidget<3> widMeasIGrid(widMeasIContents);
LabelFrameWidget widMeasIFrame(&widMeasIGrid, "MEAS I", Font3x5, kContrastBackground);

Widget* widMeasContents[] = {&widMeasVFrame, &widMeasIFrame};
HGridWidget<2> widMeas(widMeasContents);


StaleNumericTextWidget widSetV(0, 2, 100 * 1000, Font5x7, kContrastActive, kContrastStale, Font3x5, 1000, 2);
TextWidget widDacVSep(" ", 0, Font5x7, kContrastStale);
NumericTextWidget widDacV(0, 4, Font3x5, kContrastStale);
Widget* widSetVContents[] = {&widSetV, &widDacVSep, &widDacV};
HGridWidget<3> widSetVGrid(widSetVContents);
LabelFrameWidget widSetVFrame(&widSetVGrid, "V", Font3x5, kContrastBackground);

StaleNumericTextWidget widSetISrc(0, 2, 100 * 1000, Font5x7, kContrastActive, kContrastStale, Font3x5, 1000, 2);
TextWidget widDacISrcSep(" ", 0, Font5x7, kContrastStale);
NumericTextWidget widDacISrc(0, 4, Font3x5, kContrastStale);
Widget* widSetISrcCntents[] = {&widSetISrc, &widDacISrcSep, &widDacISrc};
HGridWidget<3> widSetISrcGrid(widSetISrcCntents);
LabelFrameWidget widSetISrcFrame(&widSetISrcGrid, "I SRC", Font3x5, kContrastBackground);

StaleNumericTextWidget widSetISnk(0, 2, 100 * 1000, Font5x7, kContrastActive, kContrastStale, Font3x5, 1000, 2);
TextWidget widDacISnkSep(" ", 0, Font5x7, kContrastStale);
NumericTextWidget widDacISnk(0, 4, Font3x5, kContrastStale);
Widget* widSetISnkContents[] = {&widSetISnk, &widDacISnkSep, &widDacISnk};
HGridWidget<3> widSetISnkGrid(widSetISnkContents);
LabelFrameWidget widSetISnkFrame(&widSetISnkGrid, "I SNK", Font3x5, kContrastBackground);

Widget* widSetContents[] = {&widSetVFrame, &widSetISrcFrame, &widSetISnkFrame};
HGridWidget<3> widSet(widSetContents);


TextWidget widBtns("U U U", 5, Font5x7, kContrastActive);
LabelFrameWidget widBtnsFrame(&widBtns, "BTNS", Font3x5, kContrastBackground);

NumericTextWidget pdStatus(0, 4, Font3x5, kContrastStale);
LabelFrameWidget pdStatusFrame(&pdStatus, "USB PD", Font3x5, kContrastBackground);

Widget* widMainContents[] = {&widVersionData, &widBuildData, &widMeas, &widSet, &widBtnsFrame, &pdStatusFrame};
VGridWidget<6> widMain(widMainContents);

int32_t kVoltRatio = 22148;  // 1000x, actually ~22.148 Vout / Vmeas
int32_t kAmpRatio = 10000;  // 1000x, actually 10 Aout / Vmeas

int main() {
  swdConsole.baud(115200);

  debugInfo("\r\n\r\n\r\n");
  debugInfo("USB PD SMU");
  debugInfo("Built " __DATE__ " " __TIME__ " " COMPILERNAME);
  if (Wdt.causedReset()) {
    debugWarn("WDT Reset");
  }

  UsTimer.start();

  Lcd.init();

  
  SharedI2c.frequency(400000);

  uint8_t regOut;
  if (!UsbPd.readId(regOut)) {
    debugInfo("PD ID read = %02x", regOut);
  } else {
    debugInfo("PD ID read fail");
  }
  wait_ns(500);  // 0.5us between start and stops

  int ret;
  // ret = UsbPd.writeRegister(Fusb302::Register::kReset, 0x01);  // reset everything
  // if (ret) { 
  //   debugInfo("PD Reset Set Fail: %i", ret);
  // }
  // wait_ns(500);  // 0.5us between start and stops

  // ret = UsbPd.writeRegister(Fusb302::Register::kReset, 0x00);  // reset everything
  // if (ret) { 
  //   debugInfo("PD Reset Clear Fail: %i", ret);
  // }
  // wait_ns(500);  // 0.5us between start and stops

  ret = UsbPd.writeRegister(Fusb302::Register::kPower, 0x0f);  // power up everything
  if (ret) {
    debugInfo("PD Power Set Fail: %i", ret);
  }
  wait_ns(500);  // 0.5us between start and stops

  ret = UsbPd.writeRegister(Fusb302::Register::kControl0, 0x04);  // unmask interrupts
  if (ret) {
    debugInfo("PD Control0 Set Fail: %i", ret);
  }
  wait_ns(500);  // 0.5us between start and stops

  ret = UsbPd.writeRegister(Fusb302::Register::kSwitches0, 0x07);  // meas CC1
  if (ret) {
    debugInfo("PD Switches0 Set Fail: %i", ret);
  }
  wait_ns(500);  // 0.5us between start and stops

  ret = UsbPd.writeRegister(Fusb302::Register::kSwitches1, 0x25);  // enable auto-CRC + transmitter CC1
  if (ret) {
    debugInfo("PD Switches1 Set Fail: %i", ret);
  }
  wait_ns(500);  // 0.5us between start and stops

  ret = UsbPd.writeRegister(Fusb302::Register::kControl3, 0x07);  // enable auto-retry
  if (ret) {
    debugInfo("PD AutoRetry Fail: %i", ret);
  }
  wait_ns(500);  // 0.5us between start and stops

  ret = UsbPd.writeRegister(Fusb302::Register::kReset, 0x02);  // reset PD logic
  if (ret) { 
    debugInfo("PD Reset Fail: %i", ret);
  }
  wait_ns(500);  // 0.5us between start and stops

  wait_ms(100);

  TimerTicker PdSendTicker(1000 * 1000, UsTimer);

  uint8_t i = 0;
  while (1) {
    if (LedStatusTicker.checkExpired()) {
      if (i == 0) {
        StatusLed.pulse(RgbActivity::kRed);
      } else if (i == 1) {
        StatusLed.pulse(RgbActivity::kGreen);
      } else if (i == 2) {
        StatusLed.pulse(RgbActivity::kBlue);
      }
      i = (i + 1) % 3;
    }

    StatusLed.update();

    if (PdInt == 0) {
      uint8_t pdStatus[7];
      debugInfo("PD Int");
      if (!(ret = UsbPd.readRegister(Fusb302::Register::kStatus0a, 7, pdStatus))) {
        debugInfo("PD Status 0A/1A  0x %02x %02x", pdStatus[0], pdStatus[1]);
        debugInfo("PD Interrupt A/B 0x %02x %02x", pdStatus[2], pdStatus[3]);
        debugInfo("PD Status 0/1    0x %02x %02x", pdStatus[4], pdStatus[5]);
        debugInfo("PD Interrupt     0x %02x", pdStatus[6]);
      } else {
        debugInfo("PD Status Fail = %i", ret);
      }
      wait_ns(500);  // 0.5us between start and stops
    }

    if (PdSendTicker.checkExpired()) {
      ret = UsbPd.writeRegister(Fusb302::Register::kControl0, 0x44);  // flush TX
      if (ret) {
        debugInfo("PD Flush Fail: %i", ret);
      }
      wait_ns(500);  // 0.5us between start and stops

      uint8_t payload[20];
      payload[0] = Fusb302::sopSet[0];
      payload[1] = Fusb302::sopSet[1];
      payload[2] = Fusb302::sopSet[2];
      payload[3] = Fusb302::sopSet[3];
      payload[4] = Fusb302::kFifoTokens::kPackSym | 2;
      uint16_t header = UsbPd::makeHeader(UsbPd::ControlMessageType::kGetSourceCap, 0, 0);
      payload[5] = header & 0xff;  // little-endian conversion
      payload[6] = (header >> 8) & 0xff;
      payload[7] = Fusb302::kFifoTokens::kJamCrc;
      payload[8] = Fusb302::kFifoTokens::kEop;
      payload[9] = Fusb302::kFifoTokens::kTxOff;
      payload[10] = Fusb302::kFifoTokens::kTxOn;
      debugInfo("%02x %02x %02x %02x  %02x  %02x %02x  %02x %02x %02x  %02x", 
          payload[0], payload[1], payload[2], payload[3],
          payload[4],
          payload[5], payload[6],
          payload[7], payload[8], payload[9], payload[10]);

      ret = UsbPd.writeRegister(Fusb302::Register::kFifos, 11, payload);
      if (ret) {
        debugInfo("PD TX Fail: %i", ret);
      }
      wait_ns(500);  // 0.5us between start and stops

      ret = UsbPd.writeRegister(Fusb302::Register::kControl0, 0x05);  // manual TX start
      if (ret) {
        debugInfo("PD TX Start: %i", ret);
      }
      wait_ns(500);

      uint8_t pdStatus[2];
      if (!(ret = UsbPd.readRegister(Fusb302::Register::kStatus0, 2, pdStatus))) {
        if ((pdStatus[1] & 0x20) == 0) {  // RX not empty
          debugInfo("PD RX not empty");
        }
      } else {
        debugInfo("PD Status Fail = %i", ret);
      }
      wait_ns(500);  // 0.5us between start and stops
    }

    if (LcdUpdateTicker.checkExpired()) {
      DacLdac = 1;

      SharedSpi.frequency(100000);
      
      uint16_t adcv = AdcVolt.read_raw_u12();
      int32_t measMv = ((int64_t)adcv - kAdcCenter) * 3000 * kVoltRatio / 1000 / 4096;  // in mV
      widAdcV.setValue(adcv);
      widMeasV.setValue(measMv);

      uint16_t adci = AdcCurr.read_raw_u12();
      int32_t measMa = ((int64_t)adci - kAdcCenter) * 3000 * kAmpRatio / 1000 / 4096;  // in mA
      widAdcI.setValue(adci);
      widMeasI.setValue(measMa);

      int32_t targetV = 1250;  // mV
      int32_t setVOffset = (int64_t)targetV * 4096 * 1000 / kVoltRatio / 3000;
      uint16_t setV = kDacCenter - setVOffset;
      DacVolt.write_raw_u12(setV);
      widDacV.setValue(setV);
      widSetV.setValue(targetV);

      int32_t targetISrc = 200;  // mA
      int32_t setISrcOffset = (int64_t)targetISrc * 4096 * 1000 / kAmpRatio / 3000;
      uint16_t setISrc = kDacCenter - setISrcOffset;
      DacCurrPos.write_raw_u12(setISrc);
      widDacISrc.setValue(setISrc);
      widSetISrc.setValue(targetISrc);

      int32_t targetISnk = -400;  // mA
      int32_t setISnkOffset = (int64_t)targetISnk * 4096 * 1000 / kAmpRatio / 3000;
      uint16_t setISnk = kDacCenter - setISnkOffset;
      DacCurrNeg.write_raw_u12(setISnk);
      widDacISnk.setValue(setISnk);
      widSetISnk.setValue(targetISnk);

      DacLdac = 0;

      EnableHigh = 1;
      EnableLow = 1;

      SharedSpi.frequency(10000000);

      // debugInfo("MeasV: %u => %li mV    MeasI: %u => %li mA    SetV: %u    SetISrc: %u    SetISnk %u", 
      //     adcv, measMv, adci, measMa, 
      //     setV, setISrc, setISnk)

      char btnsText[] = "U U U";
      if (SwitchL == 0) {
        btnsText[0] = 'D';
      }
      if (SwitchR == 0) {
        btnsText[2] = 'D';
      }
      if (SwitchC == 0) {
        btnsText[4] = 'D';
      }
      widBtns.setValue(btnsText);

      widMain.layout();
      widMain.draw(Lcd, 0, 0);
      Lcd.update();
    }
  }
}
