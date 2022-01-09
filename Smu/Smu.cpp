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
#include "UsbPdStateMachine.h"

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
Fusb302 FusbDevice(SharedI2c);
UsbPdStateMachine UsbPd(FusbDevice, PdInt);

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

TextWidget widVersionData("USB PD SMU", 0, Font5x7, kContrastActive);
TextWidget widBuildData("  " __DATE__, 0, Font5x7, kContrastBackground);
Widget* widVersionContents[] = {&widVersionData, &widBuildData};
HGridWidget<2> widVersionGrid(widVersionContents);

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

Widget* widMainContents[] = {&widVersionGrid, &widMeas, &widSet, &widBtnsFrame, &pdStatusFrame};
VGridWidget<5> widMain(widMainContents);

int32_t kVoltRatio = 22148;  // 1000x, actually ~22.148 Vout / Vmeas
int32_t kAmpRatio = 10000;  // 1000x, actually 10 Aout / Vmeas

template<typename T> 
class ChangeDetector {
public:
  ChangeDetector(T initValue): oldValue_(initValue) {
  }

  bool changed(T newValue, T& oldValue) {
    if (newValue != oldValue_) {
      oldValue = oldValue_;
      oldValue_ = newValue;
      return true;
    } else {
      return false;
    }
  }

protected:
  T oldValue_;
};

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

  while (1) {
    if (LedStatusTicker.checkExpired()) {
      StatusLed.pulse(RgbActivity::kBlue);
    }

    StatusLed.update();

    UsbPd.update();

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
