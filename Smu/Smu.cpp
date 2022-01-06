#include <cstdio>

#include "USBSerial.h"

#define DEBUG_ENABLED
#include "debug.h"

#include "WDT.h"

#include "RgbActivityLed.h"
#include "DmaSerial.h"
#include "LongTimer.h"

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
DigitalOut DacCurrPosCs(P0_2, 1);
DigitalOut AdcVoltCs(P0_7, 1);
DigitalOut AdcCurrCs(P0_9, 1);
DigitalOut DacVoltCs(P0_18, 1);

DigitalOut EnableHigh(P0_15);  // Current source transistor enable
DigitalOut EnableLow(P0_14);  // Current sink transistor enable

uint16_t kAdcCenter = 2042;  // Measured center value of the ADC
uint16_t kDacCenter = 2048;  // Empirically derived center value of the DAC
// uint16_t kDacCenter = 2048;

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

Widget* widMainContents[] = {&widVersionData, &widBuildData, &widMeas, &widSet, &widBtnsFrame};
VGridWidget<5> widMain(widMainContents);

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

    if (LcdUpdateTicker.checkExpired()) {
      DacLdac = 1;

      SharedSpi.frequency(100000);
      SharedSpi.format(8, 0);
      AdcVoltCs = 0;
      uint8_t adcv0 = SharedSpi.write(0);
      uint8_t adcv1 = SharedSpi.write(0);
      // first two clocks are for sampling, then one null bit, then data
      // and last bit (in a 16 bit transfer) is unused
      uint16_t adcv = (((uint16_t)(adcv0 & 0x1f) << 8) | adcv1) >> 1;
      int32_t measMv = ((int64_t)adcv - kAdcCenter) * 3000 * kVoltRatio / 1000 / 4096;  // in mV
      AdcVoltCs = 1;
      
      AdcCurrCs = 0;
      uint8_t adci0 = SharedSpi.write(0);
      uint8_t adci1 = SharedSpi.write(0);
      uint16_t adci = (((uint16_t)(adci0 & 0x1f) << 8) | adci1) >> 1;
      int32_t measMa = ((int64_t)adci - kAdcCenter) * 3000 * kAmpRatio / 1000 / 4096;  // in mA
      AdcCurrCs = 1;

      int32_t targetV = 2500;  // mV
      int32_t setVOffset = (int64_t)targetV * 4096 * 1000 / kVoltRatio / 3000;
      uint16_t setV = kDacCenter - setVOffset;
      DacVoltCs = 0;
      SharedSpi.write(0x30 | ((setV >> 8) & 0x0f));
      SharedSpi.write(setV & 0xff);
      DacVoltCs = 1;
      widSetV.setValue(targetV);
      widDacV.setValue(setV);

      int32_t targetISrc = 200;  // mA
      int32_t setISrcOffset = (int64_t)targetISrc * 4096 * 1000 / kAmpRatio / 3000;
      uint16_t setISrc = kDacCenter - setISrcOffset;
      DacCurrPosCs = 0;
      SharedSpi.write(0x30 | ((setISrc >> 8) & 0x0f));
      SharedSpi.write(setISrc & 0xff);
      DacCurrPosCs = 1;
      widSetISrc.setValue(targetISrc);
      widDacISrc.setValue(setISrc);

      int32_t targetISnk = -400;  // mA
      int32_t setISnkOffset = (int64_t)targetISnk * 4096 * 1000 / kAmpRatio / 3000;
      uint16_t setISnk = kDacCenter - setISnkOffset;
      DacCurrNegCs = 0;
      SharedSpi.write(0x30 | ((setISnk >> 8) & 0x0f));
      SharedSpi.write(setISnk & 0xff);
      DacCurrNegCs = 1;
      widSetISnk.setValue(targetISnk);
      widDacISnk.setValue(setISnk);

      DacLdac = 0;

      EnableHigh = 1;
      EnableLow = 1;

      SharedSpi.frequency(10000000);

      widAdcV.setValue(adcv);
      widAdcI.setValue(adci);
      widMeasV.setValue(measMv);
      widMeasI.setValue(measMa);
      debugInfo("MeasV: %u => %li mV    MeasI: %u => %li mA    SetV: %u    SetISrc: %u    SetISnk %u", 
          adcv, measMv, adci, measMa, 
          setV, setISrc, setISnk)

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
