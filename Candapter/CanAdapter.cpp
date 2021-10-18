#include <cstdio>

#include <can_id.h>
#include <can_struct.h>

#define DEBUG_ENABLED
#include "debug.h"

#include "WDT.h"
#include "can_buffer_timestamp.h"

#include "RgbActivityLed.h"
#include "DmaSerial.h"

#include "encoding.h"

#include "slcan.h"
#include "NonBlockingUsbSerial.h"
#include "StaticQueue.h"

#include "St7735sGraphics.h"
#include "DefaultFonts.h"
#include "Widget.h"

#include "MovingAverage.h"

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
TextWidget widBuildData("  " __DATE__ " " __TIME__, 0, Font5x7, kContrastBackground);

StaleNumericTextWidget widCanRx(0, 5, 100 * 1000, Font5x7, kContrastActive, kContrastStale);
LabelFrameWidget widCanRxFrame(&widCanRx, "RX", Font3x5, kContrastBackground);

StaleNumericTextWidget widCanErr(0, 5, 100 * 1000, Font5x7, kContrastActive, kContrastStale);
LabelFrameWidget widCanErrFrame(&widCanErr, "ERR", Font3x5, kContrastBackground);

Widget* widCanOverviewContents[] = {&widCanRxFrame, &widCanErrFrame};
HGridWidget<2> widCanOverview(widCanOverviewContents);

Widget* widMainContents[] = {&widVersionData, &widBuildData, &widCanOverview};
VGridWidget<3> widMain(widMainContents);


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

  // CAN aggregate statistics
  uint16_t thisCanRxCount = 0;
  uint16_t thisCanErrCount = 0;
  MovingAverage<uint16_t, uint32_t, 8> canRxCounter;
  MovingAverage<uint16_t, uint32_t, 8> canErrCounter;  // TODO there should be a integrating moving average

  while (1) {
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

    if (CanCheckTicker.checkExpired()) {
      if (LPC_C_CAN0->CANCNTL & (1 << 0)) {
        LPC_C_CAN0->CANCNTL &= ~(1 << 0);
        CanStatusLed.pulse(RgbActivity::kBlue);
      }
    }

    Timestamped_CANMessage msg;
    while (CanBuffer.read(msg)) {
      if (msg.isError) {
        thisCanErrCount++;
        widCanErr.fresh();
        CanStatusLed.pulse(RgbActivity::kRed);
      } else {
        if (UsbSerial.connected()) {
          uint8_t buffer[TachyonEncoding::MAX_ENCODED_SIZE];
          uint32_t len = TachyonEncoding::encode(msg.data.msg, buffer);

          if (UsbSerial.writeBlockNB(buffer, len)) {
            UsbStatusLed.pulse(RgbActivity::kYellow);
            UsbStatusTicker.reset();
          } else {
            UsbStatusLed.pulse(RgbActivity::kRed);
            UsbStatusTicker.reset();
          }
        }

        thisCanRxCount++;
        widCanRx.fresh();
        CanStatusLed.pulse(RgbActivity::kGreen);
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
      widMain.layout();
      widMain.draw(Lcd, 0, 0);
      Lcd.update();
    }
  }
}
