/**
 * PCF2129.h
 *
 * Created on: Oct 7, 2016
 *     Author: Avinash
 */

#ifndef __PCF2129_H__
#define __PCF2129_H__

#include <mbed.h>
#include <time.h>

/** Class abstraction for the PCF2129 */
class PCF2129 {

private:
    SPI& RTC_spi;           // Pointer to SPI object that the RTC is connected to
    DigitalOut& RTC_cs;     // Pointer to Digital Out object that the RTC CS is connected to

public:

    /** Constructor for PCF2129 object
     *
     * @param s Reference to an SPI object connected to the RTC
     * @param cs Reference to the DigitalOut object connected to the RTC CS pin
     */
    PCF2129(SPI &s, DigitalOut &cs) : RTC_spi(s), RTC_cs(cs) {
    }

    /** Read the current time and put it in time.
     * RTC MUST BE SET IN 24 HOUR MODE, not 12 hour AM/PM mode.
     *
     * @param time Output pointer
     *
     * @return if the oscillator hasn't stopped (if the OSF bit is clear)
     */
    bool gettime(tm *time);

    /** Set the time on the RTC from the datetime object
     * RTC MUST BE SET IN 24 HOUR MODE, not 12 hour AM/PM mode.
     *
     * @param time Time to set
     */
    int settime(const tm& time);

    /** Enable the second interrupt
     * The INT pin will go HIGH on every second and will attach
     */
    void enable_s_int();

};

#endif // __PCF_2129_H__
