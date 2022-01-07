#include <mbed.h>

#ifndef __MCP3201_H__
#define __MCP3201_H__


// MCP3201 12-bit SPI ADC
class Mcp3201 {
public:
  Mcp3201(SPI& spi, DigitalOut& cs) : spi_(spi), cs_(cs) {
    cs_ = 1;
  }

  // reads ADC as a 12-bit value
  // frequency must be set externally, which also determines the sample rate
  uint16_t read_raw_u12() {
    spi_.format(8, 0);
    cs_ = 0;
    // first two clocks are for sampling, then one null bit, then data
    // and last bit (in a 16 bit transfer) is unused
    uint8_t byte0 = spi_.write(0);
    uint8_t byte1 = spi_.write(0);
    cs_ = 1;
    return (((uint16_t)(byte0 & 0x1f) << 8) | byte1) >> 1;
  }  

  // reads ADC scaled up to a 16-bit value
  uint16_t read_u16() {
    return read_raw_u12() * 16;
  }

protected:
  SPI& spi_;
  DigitalOut& cs_;
};

#endif  // __MCP3201_H__
