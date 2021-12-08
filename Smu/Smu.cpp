#include <cstdio>

#define DEBUG_ENABLED
#include "debug.h"

#include "WDT.h"

#include "RgbActivityLed.h"
#include "DmaSerial.h"

#include "NonBlockingUsbSerial.h"
#include "StaticQueue.h"

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
NonBlockingUSBSerial UsbSerial(0x1209, 0x0001, 0x0001, false);

//
// Debugging defs
//
SPI LcdSpi(P0_11, NC, P0_13);  // mosi, miso, sclk
DigitalOut LcdCs(P0_14);
DigitalOut LcdRs(P0_10);
DigitalOut LcdReset(P0_18);
DigitalOut LcdLed(P0_15);

DigitalOut LedR(P0_1), LedG(P0_0), LedB(P0_2);
RgbActivityDigitalOut StatusLed(UsTimer, LedR, LedG, LedB, false);
TimerTicker LedStatusTicker(1 * 1000 * 1000, UsTimer);

//
// LCD and widgets
//
St7735sGraphics<160, 80, 1, 26> Lcd(LcdSpi, LcdCs, LcdRs, LcdReset);


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
  LcdLed = 1;

  while (1) {
    if (LedStatusTicker.checkExpired()) {
      
    }

    UsbStatusLed.update();

    if (LcdTicker.checkExpired()) {
      widMain.layout();
      widMain.draw(Lcd, 0, 0);
      Lcd.update();
    }
  }
}
