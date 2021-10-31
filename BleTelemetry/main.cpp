#include <mbed.h>

// Stock pin maps for Adafruit Feather nRF52480 Express
// See schematics at https://cdn-learn.adafruit.com/assets/assets/000/068/545/original/circuitpython_nRF52840_Schematic_REV-D.png

DigitalOut Led1(P1_15);  // red, active-high
DigitalOut Led2(P1_10);  // blue, active-high

AnalogIn VDiv(P0_29);  // 1/2 divide from Vbat

DigitalOut Neopixel(P0_16);  // WS2812B


SPI QSpiFlash(P0_17, P0_22, P0_19);  // mosi / D0, miso / D1, sck
DigitalOut QSpiFlashCs(P0_20);
DigitalOut QSpiFlashWp(P0_23);  // D2
DigitalOut QSpiFlashHold(P0_21);  // D3


int main() {
    while (1) {
        Led1 = !Led1;
        Led2 = !Led1;
        wait_ms(500);
    }
}
