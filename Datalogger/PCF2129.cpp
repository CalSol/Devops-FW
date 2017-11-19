/**
 * PCF2129.cpp
 *
 * Created on: Oct 7, 2016
 *     Author: Avinash
 */

#include "PCF2129.h"

static uint8_t intToBcd(uint8_t in) {
  return (((in / 10) % 10) << 4)
      | (in % 10);
}

static uint8_t bcdToInt(uint8_t in) {
  return ((in >> 4) & 0xf) * 10
      + (in & 0xf);
}

bool PCF2129::gettime(tm *time) {
    int command = 0b10100000;           //Read starting at the sec register
    RTC_cs = 0;
    wait_us(1);
    /** Write in command, then read the registers holding the time componenets */
    RTC_spi.write(command | 0x03);
    uint8_t sec = RTC_spi.write(0x00);
    uint8_t min = RTC_spi.write(0x00);
    uint8_t hour = RTC_spi.write(0x00);
    uint8_t day = RTC_spi.write(0x00);
    uint8_t weekday = RTC_spi.write(0x00);
    uint8_t month = RTC_spi.write(0x00);
    uint8_t year = RTC_spi.write(0x00);
    RTC_cs = 1;

    // Convert from device's BCD (binary coded decimal) format to int
    time->tm_sec = bcdToInt(sec & 0x7f);
    time->tm_min = bcdToInt(min & 0x7f);
    time->tm_hour = bcdToInt(hour & 0x3f);
    time->tm_mday = bcdToInt(day & 0x3f);
    time->tm_mon = bcdToInt(month & 0x1f) - 1;
    time->tm_year = bcdToInt(year) + 100;
    return !(sec & 0x80);  // OSF bit
}

int PCF2129::settime(const tm &time) {
    uint8_t command = 0b00100000;           //Write starting at the sec register
    // Convert to device's BCD (binary coded decimal) format
    uint8_t sec = intToBcd(time.tm_sec);
    uint8_t min = intToBcd(time.tm_min);
    uint8_t hour = intToBcd(time.tm_hour);
    uint8_t day = intToBcd(time.tm_mday);
    uint8_t month = intToBcd(time.tm_mon) + 1;
    uint8_t year = intToBcd(time.tm_year);

    /** Write this out to the RTC */
    RTC_cs = 0;
    wait_us(1);
    RTC_spi.write(command | 0x03);
    RTC_spi.write(sec);
    RTC_spi.write(min);
    RTC_spi.write(hour);
    RTC_spi.write(day);
    RTC_spi.write(0);
    RTC_spi.write(month);
    RTC_spi.write(year);
    RTC_cs = 1;
}

void PCF2129::enable_s_int() {
    /** Set the configuration to enable the second interrupt */
    int command = 0b00100000;
    RTC_cs=0;
    wait_us(1);
    RTC_spi.write(command | 0x10);
    RTC_spi.write(1<<5);
    RTC_cs=1;
    wait_us(5);
    RTC_cs=0;
    wait_us(1);
    RTC_spi.write(command | 0x00);
    RTC_spi.write(0x01);
    RTC_cs=1;
}
