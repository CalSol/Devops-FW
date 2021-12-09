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

DigitalOut EnableLow(P0_14);
DigitalOut EnableHigh(P0_15);

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

StaleNumericTextWidget widMeasV(0, 5, 100 * 1000, Font5x7, kContrastActive, kContrastStale, Font3x5, 1000, 3);
LabelFrameWidget widMeasVFrame(&widMeasV, "MEAS V", Font3x5, kContrastBackground);
StaleNumericTextWidget widMeasI(0, 5, 100 * 1000, Font5x7, kContrastActive, kContrastStale, Font3x5, 1000, 3);
LabelFrameWidget widMeasIFrame(&widMeasI, "MEAS I", Font3x5, kContrastBackground);
Widget* widMeasContents[] = {&widMeasVFrame, &widMeasIFrame};
HGridWidget<2> widMeas(widMeasContents);

StaleNumericTextWidget widSetV(0, 5, 100 * 1000, Font5x7, kContrastActive, kContrastStale, Font3x5, 1000, 3);
LabelFrameWidget widSetVFrame(&widSetV, "V", Font3x5, kContrastBackground);
StaleNumericTextWidget widSetISrc(0, 5, 100 * 1000, Font5x7, kContrastActive, kContrastStale, Font3x5, 1000, 3);
LabelFrameWidget widSetISrcFrame(&widSetISrc, "I SRC", Font3x5, kContrastBackground);
StaleNumericTextWidget widSetISnk(0, 5, 100 * 1000, Font5x7, kContrastActive, kContrastStale, Font3x5, 1000, 3);
LabelFrameWidget widSetISnkFrame(&widSetISnk, "I SNK", Font3x5, kContrastBackground);
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
      SharedSpi.frequency(100000);
      SharedSpi.format(8, 0);
      AdcVoltCs = 0;
      uint8_t adcv0 = SharedSpi.write(0);
      uint8_t adcv1 = SharedSpi.write(0);
      // first two clocks are for sampling, then one null bit, then data
      // and last bit (in a 16 bit transfer) is unused
      uint16_t adcv = (((uint16_t)(adcv0 & 0x1f) << 8) | adcv1) >> 1;
      int32_t measMv = ((int32_t)adcv - 2048) * 3000 * kVoltRatio / 1000 / 4096;  // in mV
      AdcVoltCs = 1;
      
      AdcCurrCs = 0;
      uint8_t adci0 = SharedSpi.write(0);
      uint8_t adci1 = SharedSpi.write(0);
      uint16_t adci = (((uint16_t)(adci0 & 0x1f) << 8) | adci1) >> 1;
      int32_t measMa = ((int32_t)adci - 2048) * 3000 * kAmpRatio / 1000 / 4096;  // in mA
      AdcCurrCs = 1;

      SharedSpi.frequency(10000000);

      widMeasV.setValue(measMv);
      widMeasI.setValue(measMa);
      debugInfo("MeasV: %u => %li mV    MeasI: %u => %li mA", adcv, measMv, adci, measMa)

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
