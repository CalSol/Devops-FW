#include <cstdio>

#define DEBUG_ENABLED
#include "debug.h"

#include "WDT.h"
#include "can_buffer_timestamp.h"

#include "RgbActivityLed.h"
#include "DmaSerial.h"

#include "slcan.h"
#include "NonBlockingUsbSerial.h"

#include "St7735sGraphics.h"
#include "DefaultFonts.h"
#include "Widget.h"

#include "MovingAverage.h"

/*
 * Local peripheral definitions
 */
const uint32_t CAN_FREQUENCY = 1000000;

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
DigitalIn SwitchCan(P0_29, PinMode::PullUp);

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

StaleNumericTextWidget widCanRx(0, 5, 100 * 1000, Font5x7, kContrastActive, kContrastStale);
LabelFrameWidget widCanRxFrame(&widCanRx, "RX", Font3x5, kContrastBackground);

StaleNumericTextWidget widCanErr(0, 5, 100 * 1000, Font5x7, kContrastActive, kContrastStale);
LabelFrameWidget widCanErrFrame(&widCanErr, "ERR", Font3x5, kContrastBackground);

Widget* widCanOverviewContents[] = {&widCanRxFrame, &widCanErrFrame};
HGridWidget<2> widCanOverview(widCanOverviewContents);

Widget* widMainContents[] = {&widVersionGrid, &widCanOverview};
VGridWidget<2> widMain(widMainContents);


TextWidget widBootData("BOOT", 0, Font5x7, kContrastActive);
Widget* widBootContents[] = {&widVersionGrid, &widBootData};
VGridWidget<2> widBoot(widBootContents);


// Helper to allow the host to send CAN messages
static bool transmitCANMessage(const CANMessage& msg) {
  CanStatusLed.pulse(RgbActivity::kYellow);
  debugInfo("TXReq %03x", msg.id);
  return Can.write(msg);
}

static bool setBaudrate(int baudrate) {
  debugInfo("Set baud = %i", baudrate);
  return Can.frequency(baudrate) == 1;
}

static bool setMode(CAN::Mode mode) {
  debugInfo("Set mode = %i", mode);
  return Can.mode(mode) == 1;
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
      switch (canIndex) {
        case 1:  CanStatusLed.pulse(RgbActivity::kRed);  break;
        case 2:  CanStatusLed.pulse(RgbActivity::kGreen);  break;
        case 3:  CanStatusLed.pulse(RgbActivity::kBlue);  break;
        default:  CanStatusLed.pulse(RgbActivity::kOff);  break;
      }
      canIndex = (canIndex + 1) % 4;
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
  CanStatTicker.reset();

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

    Timestamped_CANMessage msg;
    while (CanBuffer.read(msg)) {
      if (!msg.isError) {
        Slcan.putCANMessage(msg.data.msg);
        thisCanRxCount++;
        widCanRx.fresh();
        CanStatusLed.pulse(RgbActivity::kGreen);
        debugInfo("RXMeg %03x", msg.data.msg.id);
      } else {
        thisCanErrCount++;
        widCanErr.fresh();
        CanStatusLed.pulse(RgbActivity::kRed);
        debugInfo("RXErr %03x", msg.data.errId);
      }
    }

    if (UsbSerial.connected()) {
      UsbStatusLed.setIdle(RgbActivity::kGreen);
    } else if (UsbSerial.configured()) {
      UsbStatusLed.setIdle(RgbActivity::kYellow);
    } else {
      UsbSerial.connect(false);
      UsbStatusLed.setIdle(RgbActivity::kRed);
    }
    if (UsbStatusTicker.checkExpired()) {  // to show liveness when there's no other activity
      UsbStatusLed.pulse(RgbActivity::kOff);
    }

    // TODO USB activity lights, but as currently SLCAN completely encapsulates the USB interface
    if (UsbSerial.connected()) {
      Slcan.update();
    } else {
      Slcan.reset();
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
