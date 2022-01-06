#include <mbed.h>

#ifndef __FUSB302_H__
#define __FUSB302_H__

// FUSB302 USB Type-C PD controrller
class Fusb302 {
public:
  Fusb302(I2C& i2c, DigitalIn& interrupt) : i2c_(i2c), int_(interrupt) {
  }

  // Low-level register read function, returning the I2C error code (if nonzero) or zero (on success)
  int readRegister(uint8_t addr, size_t len, uint8_t data[]) {
    int status = i2c_.write(kI2cAddr, (const char*)&addr, len, true);
    if (status == 0) {
      status = i2c_.read(kI2cAddr, (char*)data, len);
      return status;
    } else {
      return status;
    }
  }

  // Reads the device version / revision ID, returning whether successful or not.
  int readId(uint8_t& idOut) {
    return readRegister(RegisterAddress::kDeviceId, 1, &idOut);
  }


protected:
  I2C& i2c_;
  DigitalIn& int_;

  uint8_t kI2cAddr = 0x44;

  enum RegisterAddress {
      kDeviceId = 0x01,
  };
};

#endif  // __FUSB302_H__
