#include <cstdio>

#define DEBUG_ENABLED
#include "debug.h"

#include "WDT.h"
#include "can_buffer_timestamp.h"

#include "RgbActivityLed.h"
#include "DmaSerial.h"
#include "ButtonGesture.h"

#include "NonBlockingUsbSerial.h"
#include "encoding.h"  // for telemetry emulation mode
#include "slcan.h"

#include "St7735sGraphics.h"
#include "DefaultFonts.h"
#include "Widget.h"

#include "MovingAverage.h"

/*
 * Local peripheral definitions
 */
const uint32_t CAN_FREQUENCY = 500000;

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
CAN Can(P0_9, P0_7, CAN_FREQUENCY);  // rx, tx
CANTimestampedRxBuffer<128> CanBuffer(Can, Timestamp);
TimerTicker CanCheckTicker(1 * 1000 * 1000, UsTimer);

NonBlockingUSBSerial UsbSerial(0x1209, 0x0001, 0x0001, false);
USBSLCANSlave Slcan(UsbSerial);

DigitalIn SwitchUsb(P0_17, PinMode::PullUp);
ButtonGesture SwitchUsbGesture(SwitchUsb);
DigitalIn SwitchCan(P0_29, PinMode::PullUp);
ButtonGesture SwitchCanGesture(SwitchCan);

//
// Debugging defs
//
SPI LcdSpi(P0_11, NC, P0_13);  // mosi, miso, sclk
DigitalOut LcdCs(P0_14);
DigitalOut LcdRs(P0_10);
DigitalOut LcdReset(P0_18);
DigitalOut LcdLed(P0_15);

DigitalOut UsbLedR(P0_1), UsbLedG(P0_0), UsbLedB(P0_2);
RgbActivityDigitalOut UsbStatusLed(UsTimer, UsbLedR, UsbLedG, UsbLedB, false);
TimerTicker UsbStatusTicker(1 * 1000 * 1000, UsTimer);

DigitalOut CanLedR(P0_5), CanLedG(P0_3), CanLedB(P0_6);
RgbActivityDigitalOut CanStatusLed(UsTimer, CanLedR, CanLedG, CanLedB, false);

//
// LCD and widgets
//
St7735sGraphics<160, 80, 1, 26> Lcd(LcdSpi, LcdCs, LcdRs, LcdReset);
TimerTicker LcdTicker(100 * 1000, UsTimer);
TimerTicker CanStatTicker(125 * 1000, UsTimer);

const uint8_t kContrastActive = 255;
const uint8_t kContrastStale = 191;

const uint8_t kContrastBackground = 191;

TextWidget widVersionData("CAN ADAPTER ", 0, Font5x7, kContrastActive);
TextWidget widBuildData("  " __DATE__, 0, Font5x7, kContrastBackground);
Widget* widVersionContents[] = {&widVersionData, &widBuildData};
HGridWidget<2> widVersionGrid(widVersionContents);

TextWidget widCanMode("NORMAL", 6, Font5x7, kContrastActive);
LabelFrameWidget widCanModeFrame(&widCanMode, "MODE", Font3x5, kContrastBackground);
NumericTextWidget widCanFreq(CAN_FREQUENCY, 7, Font5x7, kContrastActive);
LabelFrameWidget widCanFreqFrame(&widCanFreq, "FREQ", Font3x5, kContrastBackground);
Widget* widCanConfigContents[] = {&widCanModeFrame, &widCanFreqFrame};
HGridWidget<2> widCanConfig(widCanConfigContents);

StaleNumericTextWidget widCanRx(0, 5, 100 * 1000, Font5x7, kContrastActive, kContrastStale);
LabelFrameWidget widCanRxFrame(&widCanRx, "RX", Font3x5, kContrastBackground);
StaleNumericTextWidget widCanErr(0, 5, 100 * 1000, Font5x7, kContrastActive, kContrastStale);
LabelFrameWidget widCanErrFrame(&widCanErr, "ERR", Font3x5, kContrastBackground);
Widget* widCanOverviewContents[] = {&widCanRxFrame, &widCanErrFrame};
HGridWidget<2> widCanOverview(widCanOverviewContents);

TextWidget widUsbStatus("     ", 5, Font5x7, kContrastActive);
LabelFrameWidget widUsbStatusFrame(&widUsbStatus, "USB", Font3x5, kContrastBackground);
TextWidget widUsbInterface("     ", 5, Font5x7, kContrastActive);
LabelFrameWidget widUsbInterfaceFrame(&widUsbInterface, "INTERFACE", Font3x5, kContrastBackground);
Widget* widCanConnectionContents[] = {&widUsbStatusFrame, &widUsbInterfaceFrame};
HGridWidget<2> widCanConnection(widCanConnectionContents);

Widget* widMainContents[] = {&widVersionGrid, &widCanConfig, &widCanOverview, &widCanConnection};
VGridWidget<4> widMain(widMainContents);


TextWidget widBootData("BOOT", 0, Font5x7, kContrastActive);
Widget* widBootContents[] = {&widVersionGrid, &widBootData};
VGridWidget<2> widBoot(widBootContents);


// Helper to allow the host to send CAN messages
static bool transmitCANMessage(const CANMessage& msg) {
  CanStatusLed.pulse(RgbActivity::kYellow);
  debugInfo("TXReq %03lx %s %d", msg.id, (msg.type == CANStandard) ? "S" : "E", msg.len);
  return Can.write(msg);
}

static bool setBaudrate(int baudrate) {
  debugInfo("Set baud = %i", baudrate);
  bool success = Can.frequency(baudrate) == 1;
  if (success) {
    widCanFreq.setValue(baudrate);
  }
  return success;
}

static bool setMode(CAN::Mode mode) {
  debugInfo("Set mode = %i", mode);
  bool success = Can.mode(mode) == 1;
  if (success) {
    if (mode == CAN::Mode::Reset) {
      widCanMode.setValue("RESET");
    } else if (mode == CAN::Mode::Normal) {
      widCanMode.setValue("NORMAL");
    } else if (mode == CAN::Mode::Silent) {
      widCanMode.setValue("SILENT");
    } else if (mode == CAN::Mode::LocalTest) {
      widCanMode.setValue("LTEST");
    } else if (mode == CAN::Mode::GlobalTest) {
      widCanMode.setValue("GTEST");
    } else if (mode == CAN::Mode::SilentTest) {
      widCanMode.setValue("STEST");
    } else {
      widCanMode.setValue("UNK");
    }
  }
  return success;
}

void selfTest() {
  uint8_t usbIndex = 0, canIndex = 0;
  while (!SwitchUsb || !SwitchCan) {
    if (UsbStatusTicker.checkExpired()) {  // to show liveness when there's no other activity
      switch (usbIndex) {
        case 1:  UsbStatusLed.pulse(RgbActivity::kRed);  break;
        case 2:  UsbStatusLed.pulse(RgbActivity::kGreen);  break;
        case 3:  UsbStatusLed.pulse(RgbActivity::kBlue);  break;
        default:  UsbStatusLed.pulse(RgbActivity::kOff);  break;
      }
      usbIndex = (usbIndex + 1) % 4;
    }
    if (CanCheckTicker.checkExpired()) {  // to show liveness when there's no other activity
      volatile uint32_t canTestReg;
      switch (canIndex) {
        case 1:  CanStatusLed.pulse(RgbActivity::kRed);  break;
        case 2:  CanStatusLed.pulse(RgbActivity::kGreen);  break;
        case 3:  CanStatusLed.pulse(RgbActivity::kBlue);  break;
        case 4:  break;
        case 5:  // drive high / recessive
            LPC_C_CAN0->CANCNTL |= (1 << 7);  // enable test mode
            LPC_C_CAN0->CANTEST = (LPC_C_CAN0->CANTEST & ~0x0060) | (0x03 << 5);
            wait_ns(500);
            canTestReg = LPC_C_CAN0->CANTEST;
            if ((canTestReg & (1 << 7)) == 0) {  // TODO this agrees with the datasheet but not empirical observations
              CanStatusLed.pulse(RgbActivity::kGreen);
            } else {
              CanStatusLed.pulse(RgbActivity::kRed);
            }
            break;  
        case 6:  
            LPC_C_CAN0->CANTEST = (LPC_C_CAN0->CANTEST & ~0x0060) | (0x02 << 5);
            wait_ns(500);
            canTestReg = LPC_C_CAN0->CANTEST;
            if ((canTestReg & (1 << 7)) == (1 << 7)) {  // TODO this agrees with the datasheet but not empirical observations
              CanStatusLed.pulse(RgbActivity::kGreen);
            } else {
              CanStatusLed.pulse(RgbActivity::kRed);
            }
            break;
        case 7:  break;
        default:  CanStatusLed.pulse(RgbActivity::kOff);  break;
      }
      canIndex = (canIndex + 1) % 8;
    }

    if (LcdTicker.checkExpired()) {
      Lcd.clear();
      widBoot.layout();
      widBoot.draw(Lcd, 0, 0);
      Lcd.update();
    }

    UsbStatusLed.update();
    CanStatusLed.update();
  }
  LPC_C_CAN0->CANTEST = LPC_C_CAN0->CANTEST & ~0x0060;  // return control of CAN to controller
  LPC_C_CAN0->CANCNTL &= ~(1 << 7);  // disable test mode
  CanStatTicker.reset();
}

int main() {
  swdConsole.baud(115200);

  debugInfo("\r\n\r\n\r\n");
  debugInfo("Candapter");
  debugInfo("Built " __DATE__ " " __TIME__ " " COMPILERNAME);
  if (Wdt.causedReset()) {
    debugWarn("WDT Reset");
  }

  UsTimer.start();

  Lcd.init();
  LcdLed = 1;

  selfTest();

  bool inTelemetryMode = false;  // false = SLCAN mode, true = telemetry emulation mode

  // Allow the SLCAN interface to transmit messages
  Slcan.setTransmitHandler(&transmitCANMessage);
  Slcan.setBaudrateHandler(&setBaudrate);
  Slcan.setModeHandler(&setMode);

  // CAN aggregate statistics
  uint16_t thisCanRxCount = 0;
  uint16_t thisCanErrCount = 0;
  MovingAverage<uint16_t, uint32_t, 8> canRxCounter;
  MovingAverage<uint16_t, uint32_t, 8> canErrCounter;

  while (1) {
    if (CanCheckTicker.checkExpired()) {
      if (LPC_C_CAN0->CANCNTL & (1 << 0)) {
        LPC_C_CAN0->CANCNTL &= ~(1 << 0);
        CanStatusLed.pulse(RgbActivity::kBlue);
        debugInfo("CAN reset");
      }
    }

    switch (SwitchCanGesture.update()) {
      case ButtonGesture::Gesture::kClickPress:
        Can.write(CANMessage(42));
        CanStatusLed.pulse(RgbActivity::kYellow);
        break;
      default: break;
    }

    switch (SwitchUsbGesture.update()) {
      case ButtonGesture::Gesture::kHoldTransition:
        inTelemetryMode = !inTelemetryMode;
        break;
      default: break;
    }

    Timestamped_CANMessage msg;
    while (CanBuffer.read(msg)) {
      if (!msg.isError) {  
        if (UsbSerial.connected()) {
          if (!inTelemetryMode) {
            if (Slcan.putCANMessage(msg.data.msg)) {
              UsbStatusLed.pulse(RgbActivity::kOff);
            } else {
              UsbStatusLed.pulse(RgbActivity::kRed);
            }
            UsbStatusTicker.reset();
          } else {
            uint8_t buffer[TachyonEncoding::MAX_ENCODED_SIZE];
            uint32_t len = TachyonEncoding::encode(msg.data.msg, buffer);
            if (UsbSerial.writeBlockNB(buffer, len)) {
              UsbStatusLed.pulse(RgbActivity::kOff);
            } else {
              UsbStatusLed.pulse(RgbActivity::kRed);
            }
            UsbStatusTicker.reset();
          }
        }
        thisCanRxCount++;
        widCanRx.fresh();
        CanStatusLed.pulse(RgbActivity::kGreen);
        debugInfo("RXMsg %03lx %s %d", msg.data.msg.id, (msg.data.msg.type == CANStandard) ? "S" : "E", msg.data.msg.len);
      } else {
        thisCanErrCount++;
        widCanErr.fresh();
        CanStatusLed.pulse(RgbActivity::kRed);
        // debugInfo("RXErr %03x", msg.data.errId);
      }
    }

    // Loopback messages don't seem to trigger interrupts, so we need to directly read the CAN block
    // TODO: can this cause a race condition with the interrupt?
    // CANMessage canMsg;
    // while (Can.read(canMsg)){
    //     Slcan.putCANMessage(canMsg);
    //     thisCanRxCount++;
    //     widCanRx.fresh();
    //     CanStatusLed.pulse(RgbActivity::kGreen);
    //     debugInfo("C RXMsg %03lx %s %d", canMsg.id, (canMsg.type == CANStandard) ? "S" : "E", canMsg.len);
    // }

    if (UsbSerial.connected()) {
      widUsbStatus.setValue(" CON ");
      UsbStatusLed.setIdle(RgbActivity::kGreen);
    } else if (UsbSerial.configured()) {
      widUsbStatus.setValue(" CNF ");
      UsbStatusLed.setIdle(RgbActivity::kYellow);
    } else {
      widUsbStatus.setValue(" DIS ");
      UsbSerial.connect(false);
      UsbStatusLed.setIdle(RgbActivity::kRed);
    }
    if (UsbStatusTicker.checkExpired()) {  // to show liveness when there's no other activity
      UsbStatusLed.pulse(RgbActivity::kYellow);
    }

    if (!inTelemetryMode) {
      widUsbInterface.setValue("SLCAN");
    } else {
      widUsbInterface.setValue("TELEM");
    }

    // TODO USB activity lights on input from PC, but as currently SLCAN completely encapsulates the USB interface
    if (inTelemetryMode) {
      if (UsbSerial.connected()) {
        Slcan.update();
      } else {
        Slcan.reset();
      }
    }

    UsbStatusLed.update();
    CanStatusLed.update();

    if (CanStatTicker.checkExpired()) {
      canRxCounter.update(thisCanRxCount);
      canErrCounter.update(thisCanErrCount);
      thisCanRxCount = 0;
      thisCanErrCount = 0;
      widCanRx.setValueStale(canRxCounter.readSum());
      widCanErr.setValueStale(canErrCounter.readSum());
    }

    if (LcdTicker.checkExpired()) {
      Lcd.clear();
      widMain.layout();
      widMain.draw(Lcd, 0, 0);
      Lcd.update();
    }
  }
}
