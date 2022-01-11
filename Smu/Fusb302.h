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
  Fusb302(I2C& i2c) : i2c_(i2c) {
  }

  // Single register write convenience wrapper
  int writeRegister(uint8_t addr, uint8_t data);
  // Low-level register write function, returning the I2C error code (if nonzero) or zero (on success)
  int writeRegister(uint8_t addr, size_t len, uint8_t data[]);

  // Low-level register read function, returning the I2C error code (if nonzero) or zero (on success)
  int readRegister(uint8_t addr, size_t len, uint8_t data[]);
  // Single register read convenience wrapper.
  int readRegister(uint8_t addr, uint8_t& dataOut);

  // Reads the device version / revision ID, returning whether successful or not.
  int readId(uint8_t& idOut);

  // Writes a message (with the specified 16-bit header and 32-bit data objects) to the TX FIFO
  // and requests its transmission.
  int writeFifoMessage(uint16_t header, uint8_t numDataObjects=0, uint32_t data[]=NULL);

  // Reads the next packet from the RX FIFO, returning 0 if a packet was read, or an error code otherwise.
  // bufferOut must be at least 30 bytes
  int readNextRxFifo(uint8_t bufferOut[]);

  enum TransferResult {
    kOk = 0x00,
    kNoDevice = 0x01,
    kNackAddr = 0x02,
    kNoDeviceRead = 0x03,
    kUnknownRxStructure = 0x04,
    kNackData = 0x10,  // + data index
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
    kMask = 0x0A,
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
  const uint8_t kRxFifoTokenMask = 0xe0;

  enum kStatus1 {
    kRxSop2 = 0x80,
    kRxSop1 = 0x40,
    kRxEmpty = 0x20,
    kRxFull = 0x10,
    kTxEmpty = 0x08,
    kTxFull = 0x04,
    kOvertemp = 0x02,
    oOcp = 0x01l
  };

  enum kInterrupt {
    kIVBusOk = 0x80,
    kIActivity = 0x40,
    kICompChng = 0x20,
    kICrcChk = 0x10,
    kIAlert = 0x08,
    kIWake = 0x04,
    kICollision = 0x02,
    kIBcLvl = 0x01,
  };

  enum kInterrupta {
    kIOcpTemp = 0x80,
    kITogDone = 0x40,
    kISoftFail = 0x20,
    kIRetryFail = 0x10,
    kIHardSent = 0x08,
    kITxSent = 0x04,
    kISoftRst = 0x02,
    kIHardRst = 0x01,
  };

  enum kInterruptb {
    kIGcrcsent = 0x01,
  };

  static constexpr kFifoTokens kSopSet[4] = {
    kFifoTokens::kSop1, 
    kFifoTokens::kSop1,
    kFifoTokens::kSop1, 
    kFifoTokens::kSop2,
  };

  static const uint32_t kStopStartDelayNs = 500;  // 0.5us between start and stops

protected:
  I2C& i2c_;
};

#endif  // __FUSB302_H__
