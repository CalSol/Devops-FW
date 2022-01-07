#include <mbed.h>

#ifndef __FUSB302_H__
#define __FUSB302_H__


// FUSB302 USB Type-C PD controller
// Useful resources:
// - FUSB302 datasheet
// - Minimal implementation: https://github.com/Ralim/usb-pd, itself based on the PD Buddy Sink code
// - Minimal implementation, links to protocol docs: https://github.com/tschiemer/usbc-pd-fusb302-d
//   - Protocol summary: https://www.embedded.com/usb-type-c-and-power-delivery-101-power-delivery-protocol/
//   - Shorter protocol summary: http://blog.teledynelecroy.com/2016/05/usb-type-c-and-power-delivery-messaging.html
class Fusb302 {
public:
  Fusb302(I2C& i2c, DigitalIn& interrupt) : i2c_(i2c), int_(interrupt) {
  }

  // Single register write convenience wrapper
  int writeRegister(uint8_t addr, uint8_t data) {
    uint8_t payload[1];
    payload[0] = data;
    return writeRegister(addr, 1, payload);
  }

  // Low-level register write function, returning the I2C error code (if nonzero) or zero (on success)
  int writeRegister(uint8_t addr, size_t len, uint8_t data[]) {
    i2c_.start();
    int ret = i2c_.write(kI2cAddr & 0xfe);  // ensure write bit is set
    if (ret != 1) {
      return TransferResult::kNoDevice;
    }
    ret = i2c_.write(addr);
    if (ret != 1) {
      return TransferResult::kNackAddr;
    }
    for (size_t i=0; i<len; i++) {
      ret = i2c_.write(data[i]);
      if (ret != 1) {
        return TransferResult::kNackData + i;
      } 
    }
    i2c_.stop();
    return 0;
  }

  // Low-level register read function, returning the I2C error code (if nonzero) or zero (on success)
  int readRegister(uint8_t addr, size_t len, uint8_t data[]) {
    int status = i2c_.write(kI2cAddr, (const char*)&addr, 1, true);
    if (status == 0) {
      status = i2c_.read(kI2cAddr, (char*)data, len);
      return status;
    } else {
      return status;
    }
  }

  // Single register read convenience wrapper.
  int readRegister(uint8_t addr, uint8_t& dataOut) {
    return readRegister(addr, 1, &dataOut);
  }

  // Reads the device version / revision ID, returning whether successful or not.
  int readId(uint8_t& idOut) {
    return readRegister(Register::kDeviceId, idOut);
  }

  enum TransferResult {
    kOk = 0x00,
    kNoDevice = 0x01,
    kNackAddr = 0x02,
    kNackData = 0x03,  // + data index
  };

  uint8_t kI2cAddr = 0x44;

  enum Register {
    kDeviceId = 0x01,
    kSwitches0 = 0x02,
    kSwitches1 = 0x03,
    kMeasure = 0x04,
    kSlice = 0x05,
    kControl0 = 0x06,
    kControl1 = 0x07,
    kControl2 = 0x08,
    kControl3 = 0x09,
    kMask1 = 0x0A,
    kPower = 0x0B,
    kReset = 0x0c,
    kOcPreg = 0x0d,
    kMaska = 0x0e,
    kMaskb = 0x0f,
    kStatus0a = 0x3c,
    kStatus1a = 0x3d,
    kInterrupta = 0x3e,
    kInterruptb = 0x3f,
    kStatus0 = 0x40,
    kStatus1 = 0x41,
    kInterrupt = 0x42,
    kFifos = 0x43,
  };

  enum kFifoTokens {
    kTxOn = 0xA1,  // FIFO should be filled with data before this is written
    kSop1 = 0x12,  // actually Sync-1
    kSop2 = 0x13,  // actually Sync-2
    kSop3 = 0x1b,  // actually Sync-3
    kReset1 = 0x15,  // RST-1
    kReset2 = 0x16,  // RST-2
    kPackSym = 0x80,  // followed by N data bytes, 5 LSBs indicates the length, 3 MSBs must be b100
    kJamCrc = 0xff,  // calculates an inserts CRC
    kEop = 0x14,  // generates EOP symbol
    kTxOff = 0xfe,  // typically right after EOP
  };
  enum kRxFifoTokens {
    kSop = 0xe0,  // only top 3 MSBs matter
  };

  static constexpr kFifoTokens sopSet[4] = {
    kFifoTokens::kSop1, 
    kFifoTokens::kSop1,
    kFifoTokens::kSop1, 
    kFifoTokens::kSop2,
  };

protected:
  I2C& i2c_;
  DigitalIn& int_;
};

#endif  // __FUSB302_H__
