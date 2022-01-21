#include <cstdio>

#include "USBSerial.h"
#include "USBHID.h"

#define DEBUG_ENABLED
#include "debug.h"

#include "WDT.h"

#include "RgbActivityLed.h"
#include "DmaSerial.h"
#include "LongTimer.h"

#include "ButtonGesture.h"

#include "Mcp3201.h"
#include "Mcp4921.h"
#include "SmuAnalogStage.h"
#include "Fusb302.h"
#include "UsbPd.h"
#include "UsbPdStateMachine.h"

#include "St7735sGraphics.h"
#include "DefaultFonts.h"
#include "Widget.h"

#include "pb_common.h"
#include "pb_encode.h"
#include "pb_decode.h"
#include "smu.pb.h"

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
// USBSerial UsbSerial(0x1209, 0x0001, 0x0001, false);
class UsbHidSmu: public USBHID {
public:
  UsbHidSmu(uint8_t output_report_length = 64, uint8_t input_report_length = 64, bool connect = true):
      USBHID(output_report_length, input_report_length, 0x1209, 0x0007, 0x0001, connect) {
  }

  // USB Device oerrides
const uint8_t * stringImanufacturerDesc() {
    static const uint8_t stringImanufacturerDescriptor[] = {
        2 + 5*2,  // bLength
        STRING_DESCRIPTOR,
        'D',0,'u',0,'c',0,'k',0,'y',0
    };
    return stringImanufacturerDescriptor;
}

const uint8_t * stringIserialDesc() {
    static const uint8_t stringIserialDescriptor[] = {
        2 + 2*2,  // bLength
        STRING_DESCRIPTOR,
        '0',0,'1',0
    };
    return stringIserialDescriptor;
}

// USB HID overrides
const uint8_t * stringIproductDesc() {
    static const uint8_t stringIproductDescriptor[] = {
        2 + 10*2,  // bLength
        STRING_DESCRIPTOR,
        'U',0,'S',0,'B',0,' ',0,'P',0,'D',0,' ',0,'S',0,'M',0,'U',0 //bString iProduct - HID device
    };
    return stringIproductDescriptor;
}

protected:
};
UsbHidSmu UsbHid(64, 64, false);

//
// System
//
SPI SharedSpi(P0_3, P0_5, P0_6);  // mosi, miso, sclk

DigitalOut DacLdac(P0_0, 1);
DigitalOut DacCurrNegCs(P0_1, 1);
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

SmuAnalogStage Smu(SharedSpi, DacVolt, DacCurrNeg, DacCurrPos, DacLdac, AdcVolt, AdcCurr, EnableHigh, EnableLow);


InterruptIn PdInt(P0_17, PinMode::PullUp);
I2C SharedI2c(P0_23, P0_22);  // sda, scl
Fusb302 FusbDevice(SharedI2c);
UsbPdStateMachine UsbPdFsm(FusbDevice, PdInt);

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
ButtonGesture SwitchLGesture(SwitchL);
DigitalIn SwitchR(P0_25, PinMode::PullUp);
ButtonGesture SwitchRGesture(SwitchR);
DigitalIn SwitchC(P0_26, PinMode::PullUp);
ButtonGesture SwitchCGesture(SwitchC);

//
// LCD and widgets
//
St7735sGraphics<160, 80, 1, 26> Lcd(SharedSpi, LcdCs, LcdRs, LcdReset);
TimerTicker MeasureTicker(50 * 1000, UsTimer);
TimerTicker LcdUpdateTicker(100 * 1000, UsTimer);

const uint8_t kContrastActive = 255;
const uint8_t kContrastStale = 191;

const uint8_t kContrastBackground = 191;

TextWidget widVersionData("USB PD SMU", 0, Font5x7, kContrastActive);
TextWidget widBuildData("  " __DATE__, 0, Font5x7, kContrastBackground);
Widget* widVersionContents[] = {&widVersionData, &widBuildData};
HGridWidget<2> widVersionGrid(widVersionContents);


TextWidget widEnable("     ", 0, Font5x7, kContrastStale);
LabelFrameWidget widEnableFrame(&widEnable, "ENABLE", Font3x5, kContrastBackground);

StaleNumericTextWidget widMeasV(0, 2, 100 * 1000, Font5x7, kContrastActive, kContrastStale, Font3x5, 1000, 2);
LabelFrameWidget widMeasVFrame(&widMeasV, "MEAS V", Font3x5, kContrastBackground);

StaleNumericTextWidget widMeasI(0, 2, 100 * 1000, Font5x7, kContrastActive, kContrastStale, Font3x5, 1000, 2);
LabelFrameWidget widMeasIFrame(&widMeasI, "MEAS I", Font3x5, kContrastBackground);

Widget* widMeasContents[] = {&widEnableFrame, &widMeasVFrame, &widMeasIFrame};
HGridWidget<3> widMeas(widMeasContents);


TextWidget widUsb("     ", 0, Font5x7, kContrastStale);
LabelFrameWidget widUsbFrame(&widUsb, "USB", Font3x5, kContrastBackground);

StaleNumericTextWidget widSetV(0, 2, 100 * 1000, Font5x7, kContrastActive, kContrastStale, Font3x5, 1000, 2);
LabelFrameWidget widSetVFrame(&widSetV, "V", Font3x5, kContrastBackground);

StaleNumericTextWidget widSetISrc(0, 2, 100 * 1000, Font5x7, kContrastActive, kContrastStale, Font3x5, 1000, 2);
LabelFrameWidget widSetISrcFrame(&widSetISrc, "I SRC", Font3x5, kContrastBackground);

StaleNumericTextWidget widSetISnk(0, 2, 100 * 1000, Font5x7, kContrastActive, kContrastStale, Font3x5, 1000, 2);
LabelFrameWidget widSetISnkFrame(&widSetISnk, "I SNK", Font3x5, kContrastBackground);

Widget* widSetContents[] = {&widUsbFrame, &widSetVFrame, &widSetISrcFrame, &widSetISnkFrame};
HGridWidget<4> widSet(widSetContents);


NumericTextWidget widPd1V(0, 2, Font5x7, 0, Font3x5, 1000, 1);
NumericTextWidget widPd1I(0, 2, Font5x7, 0, Font3x5, 1000, 1);
Widget* widPd1Contents[] = {&widPd1V, &widPd1I};
VGridWidget<2> widPd1Grid(widPd1Contents);

NumericTextWidget widPd2V(0, 2, Font5x7, 0, Font3x5, 1000, 1);
NumericTextWidget widPd2I(0, 2, Font5x7, 0, Font3x5, 1000, 1);
Widget* widPd2Contents[] = {&widPd2V, &widPd2I};
VGridWidget<2> widPd2Grid(widPd2Contents);

NumericTextWidget widPd3V(0, 2, Font5x7, 0, Font3x5, 1000, 1);
NumericTextWidget widPd3I(0, 2, Font5x7, 0, Font3x5, 1000, 1);
Widget* widPd3Contents[] = {&widPd3V, &widPd3I};
VGridWidget<2> widPd3Grid(widPd3Contents);

NumericTextWidget widPd4V(0, 2, Font5x7, 0, Font3x5, 1000, 1);
NumericTextWidget widPd4I(0, 2, Font5x7, 0, Font3x5, 1000, 1);
Widget* widPd4Contents[] = {&widPd4V, &widPd4I};
VGridWidget<2> widPd4Grid(widPd4Contents);

NumericTextWidget widPd5V(0, 2, Font5x7, 0, Font3x5, 1000, 1);
NumericTextWidget widPd5I(0, 2, Font5x7, 0, Font3x5, 1000, 1);
Widget* widPd5Contents[] = {&widPd5V, &widPd5I};
VGridWidget<2> widPd5Grid(widPd5Contents);

NumericTextWidget* widPdV[] = {&widPd1V, &widPd2V, &widPd3V, &widPd4V, &widPd5V};
NumericTextWidget* widPdI[] = {&widPd1I, &widPd2I, &widPd3I, &widPd4I, &widPd5I};

TextWidget widPdSep(" ", 0, Font5x7, kContrastStale);
Widget* widPdContents[] = {
  &widPd1Grid, &widPdSep,
  &widPd2Grid, &widPdSep,
  &widPd3Grid, &widPdSep, 
  &widPd4Grid, &widPdSep, 
  &widPd5Grid, &widPdSep
};
HGridWidget<9> widPdGrid(widPdContents);
LabelFrameWidget pdStatusFrame(&widPdGrid, "USB PD", Font3x5, kContrastBackground);

Widget* widMainContents[] = {&widVersionGrid, &widMeas, &widSet, &pdStatusFrame};
VGridWidget<4> widMain(widMainContents);


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

  int32_t targetV = 3300, targetISrc = 100, targetISnk = -100;  // mV, mA, mA
  Smu.setVoltageMv(targetV);
  Smu.setCurrentSourceMa(targetISrc);
  Smu.setCurrentSinkMa(targetISnk);

  uint8_t selected = 0;

  int32_t measMv = 0, measMa = 0;  // needed for the current limiting indicator
  
  while (1) {
    if (MeasureTicker.checkExpired()) {  // limit the ADC read frequency to avoid impedance issues
      SharedSpi.frequency(100000);
    
      measMv = Smu.readVoltageMv();
      widMeasV.setValue(measMv);

      measMa = Smu.readCurrentMa();
      widMeasI.setValue(measMa);  
    }

    if (LcdUpdateTicker.checkExpired()) {
      UsbPd::Capability::Unpacked pdCapabilities[8];
      uint8_t numCapabilities = UsbPdFsm.getCapabilities(pdCapabilities);
      uint8_t currentCapability = UsbPdFsm.currentCapability();
      for (uint8_t i=0; i<5; i++) {
        if (i < numCapabilities) {
          widPdV[i]->setValue(pdCapabilities[i].voltageMv);
          widPdI[i]->setValue(pdCapabilities[i].maxCurrentMa);
          if (i + 1 == currentCapability) {
            widPdV[i]->setContrast(kContrastActive);
            widPdI[i]->setContrast(kContrastActive);
          } else {
            widPdV[i]->setContrast(kContrastStale);
            widPdI[i]->setContrast(kContrastStale);
          }
        } else {
          widPdV[i]->setContrast(0);
          widPdI[i]->setContrast(0);
        }
      }

      Lcd.clear();
      widMain.layout();
      widMain.draw(Lcd, 0, 0);
      SharedSpi.frequency(10000000);
      Lcd.update();
    }

    bool voltageChanged = false;
    switch (SwitchLGesture.update()) {
      case ButtonGesture::Gesture::kClickPress:
      case ButtonGesture::Gesture::kHoldRepeat:
        switch (selected) {
          case 0:  targetV -= 100;  Smu.setVoltageMv(targetV);  voltageChanged = true;  break;
          case 1:  targetISrc -= 100;  Smu.setCurrentSourceMa(targetISrc);  break;
          case 2:  targetISnk -= 100;  Smu.setCurrentSinkMa(targetISnk);  break;
          default: break;
        }
        break;
      default: break;
    }
    switch (SwitchRGesture.update()) {
      case ButtonGesture::Gesture::kClickPress:
      case ButtonGesture::Gesture::kHoldRepeat:
        switch (selected) {
          case 0:  targetV += 100;  Smu.setVoltageMv(targetV);  voltageChanged = true;  break;
          case 1:  targetISrc += 100;  Smu.setCurrentSourceMa(targetISrc);  break;
          case 2:  targetISnk += 100;  Smu.setCurrentSinkMa(targetISnk);  break;
          default: break;
        }
        break;
      default: break;
    }

    if (voltageChanged) {
      UsbPd::Capability::Unpacked pdCapabilities[8];
      uint8_t numCapabilities = UsbPdFsm.getCapabilities(pdCapabilities);
      uint8_t currentCapability = UsbPdFsm.currentCapability();  // note, 1-indexed!
      if (currentCapability > 1 && pdCapabilities[currentCapability - 2].voltageMv >= targetV + 1500) {
        UsbPdFsm.requestCapability(currentCapability - 1, pdCapabilities[currentCapability - 2].maxCurrentMa);
      } else if (currentCapability < numCapabilities && 
          pdCapabilities[currentCapability - 1].voltageMv < targetV + 1500) {
        UsbPdFsm.requestCapability(currentCapability + 1, pdCapabilities[currentCapability].maxCurrentMa);
      }
    }

    HID_REPORT sendHidReport;
    bool result = false;
    switch (SwitchCGesture.update()) {
      case ButtonGesture::Gesture::kClickRelease:
        selected = (selected + 1) % 3;

        sendHidReport.length = 3;
        sendHidReport.data[0] = 0x42;
        sendHidReport.data[1] = 0x5A;
        sendHidReport.data[2] = 0xA5;
        result = UsbHid.send(&sendHidReport);
        debugInfo("Sent HID: r=%i ", result);

        break;
      case ButtonGesture::Gesture::kHoldTransition:
        if (Smu.getState() == SmuAnalogStage::SmuState::kEnabled) {
          Smu.disableDriver();
        } else {
          Smu.enableDriver();
        }
        break;
      default: break;
    }

    HID_REPORT receivedHidReport;
    if(UsbHid.configured() && UsbHid.readNB(&receivedHidReport)) {
      pb_istream_t stream = pb_istream_from_buffer(receivedHidReport.data, receivedHidReport.length);
      SmuCommand decoded;
      bool decodeSuccess = pb_decode_ex(&stream, SmuCommand_fields, &decoded, PB_DECODE_DELIMITED);
      if (!decodeSuccess) {
        debugInfo("Failed to decode");
      } else {
        debugInfo("Decoded %i", decoded.which_command);
      }

      debugInfo("Received HID: l=%li d=%02x %02x %02x %02x", receivedHidReport.length, 
          receivedHidReport.data[0], receivedHidReport.data[1], receivedHidReport.data[2], receivedHidReport.data[3]);

      SmuResponse response;
      response.which_response = SmuResponse_measurements_tag;
      response.response.measurements.voltage = measMv;
      response.response.measurements.current = measMa;
      
      sendHidReport.length = 64;  // must match HID report size
      memset(sendHidReport.data, 0, sendHidReport.length);  // clear out the excess bytes
      pb_ostream_t outStream = pb_ostream_from_buffer(sendHidReport.data, 64);
      pb_encode_ex(&outStream, SmuResponse_fields, &response, PB_ENCODE_DELIMITED);
      UsbHid.send(&sendHidReport);
    }

    widSetV.setValue(targetV);
    widSetISrc.setValue(targetISrc);
    widSetISnk.setValue(targetISnk);

    if (selected == 0) {
      widSetVFrame.setContrast(kContrastActive);
      widSetISrcFrame.setContrast(kContrastStale);
      widSetISnkFrame.setContrast(kContrastStale);
    } else if (selected == 1) {
      widSetVFrame.setContrast(kContrastStale);
      widSetISrcFrame.setContrast(kContrastActive);
      widSetISnkFrame.setContrast(kContrastStale);
    } else if (selected == 2) {
      widSetVFrame.setContrast(kContrastStale);
      widSetISrcFrame.setContrast(kContrastStale);
      widSetISnkFrame.setContrast(kContrastActive);
    } else {
      widSetVFrame.setContrast(kContrastStale);
      widSetISrcFrame.setContrast(kContrastStale);
      widSetISnkFrame.setContrast(kContrastStale);
    }

    if (Smu.getState() == SmuAnalogStage::SmuState::kEnabled) {
      EnableHigh = 1;
      widEnable.setValue(" ENA ");
      widEnable.setContrast(kContrastActive);

      if (measMv < targetV * 90 / 100) {  // guesstimate for current-limiting mode
        if (measMa >= targetISrc * 90 / 100 || measMa <= -(targetISnk * 90 / 100)) {
          StatusLed.setIdle(RgbActivity::kRed);
        } else {
          StatusLed.setIdle(RgbActivity::kPurple);
        }
      } else {
        StatusLed.setIdle(RgbActivity::kGreen);
      }
      if (LedStatusTicker.checkExpired()) {
        StatusLed.pulse(RgbActivity::kOff);
      }  
    } else {
      EnableHigh = 0;
      EnableLow = 0;
      widEnable.setValue(" DIS ");
      widEnable.setContrast(kContrastStale);

      StatusLed.setIdle(RgbActivity::kOff);
      if (LedStatusTicker.checkExpired()) {
        StatusLed.pulse(RgbActivity::kBlue);
      }
    }

    if (UsbHid.configured()) {
      widUsb.setValue(" HID ");
      widUsb.setContrast(kContrastActive);
    } else {
      widUsb.setValue(" DIS ");
      widUsb.setContrast(kContrastStale);
      UsbHid.connect(false);
    }

    UsbPdFsm.update();
    Smu.update();

    StatusLed.update();
  }
}
