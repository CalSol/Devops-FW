#include <cstdio>

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
// USB goes here

//
// Debugging defs
//
// SPI LcdSpi(P0_3, NC, P0_6);  // mosi, miso, sclk
// DigitalOut LcdCs(P0_13);
// DigitalOut LcdRs(P0_11);
// DigitalOut LcdReset(P0_10);

DigitalOut LedR(P0_29), LedG(P0_28), LedB(P0_27);
RgbActivityDigitalOut StatusLed(UsTimer, LedR, LedG, LedB, false);
TimerTicker LedStatusTicker(1 * 1000 * 1000, UsTimer);

//
// LCD and widgets
//
// St7735sGraphics<160, 80, 1, 26> Lcd(LcdSpi, LcdCs, LcdRs, LcdReset);


int main() {
  swdConsole.baud(115200);

  debugInfo("\r\n\r\n\r\n");
  debugInfo("USB PD SMU");
  debugInfo("Built " __DATE__ " " __TIME__ " " COMPILERNAME);
  if (Wdt.causedReset()) {
    debugWarn("WDT Reset");
  }

  UsTimer.start();

  // Lcd.init();

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

    // if (LcdTicker.checkExpired()) {
    //   Lcd.update();
    // }
  }
}
