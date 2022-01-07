#include <mbed.h>

#ifndef __MCP4921_H__
#define __MCP4921_H__


// MCP4921 12-bit SPI DAC
class Mcp4921 {
public:
  Mcp4921(SPI& spi, DigitalOut& cs) : spi_(spi), cs_(cs) {
    cs_ = 1;
  }

  // writes a 12-bit value to the DAC
  // LDAC must be set externally
  void write_raw_u12(uint16_t value) {
    spi_.format(8, 0);
    cs_ = 0;
    // first 4 bits: write DACA, unbuffered mode, 1x gain, enable
    spi_.write(0x30 | ((value >> 8) & 0x0f));
    spi_.write(value & 0xff);
    cs_ = 1;
  }  

  // reads ADC scaled up to a 16-bit value
  void write_u16(uint16_t value) {
    write_raw_u12(value / 16);
  }

protected:
  SPI& spi_;
  DigitalOut& cs_;
};

#endif  // __MCP4921_H__
