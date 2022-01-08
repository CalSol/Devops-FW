#include <mbed.h>

#include "UsbPd.h"


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
    wait_ns(260);
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
    wait_ns(260);
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

  int writeFifoMessage(uint16_t header, uint8_t numDataObjects=0, uint32_t data[]=NULL) {
    uint8_t buffer[38];  // 4 SOP + 1 pack sym + 2 header + 7x4 data + 1 EOP + 1 TxOff + 1 TxOn
    uint8_t bufInd = 0;
    buffer[bufInd++] = kSopSet[0];
    buffer[bufInd++] = kSopSet[1];
    buffer[bufInd++] = kSopSet[2];
    buffer[bufInd++] = kSopSet[3];
    buffer[bufInd++] = kFifoTokens::kPackSym | (2 + numDataObjects * 4);
    UsbPd::packUint16(header, buffer + bufInd);
    bufInd += 2;
    for (uint8_t i=0; i<numDataObjects; i++) {
      UsbPd::packUint32(data[i], buffer + bufInd);
      bufInd += 4;
    }
    buffer[bufInd++] = Fusb302::kFifoTokens::kJamCrc;
    buffer[bufInd++] = Fusb302::kFifoTokens::kEop;
    buffer[bufInd++] = Fusb302::kFifoTokens::kTxOff;
    buffer[bufInd++] = Fusb302::kFifoTokens::kTxOn;

    return writeRegister(Fusb302::Register::kFifos, 4 + 1 + 2 + (numDataObjects * 4) + 4, buffer);
  }

  // Reads the next packet from the RX FIFO, returning 0 if a packet was read, or an error code otherwise.
  // bufferOut must be at least 30 bytes
  int readNextRxFifo(uint8_t bufferOut[]) {
    uint8_t dump[4];  // CRC bytes
    uint8_t bufferInd = 0;

    i2c_.start();
    wait_ns(260);
    int ret = i2c_.write(kI2cAddr & 0xfe);  // ensure write bit is clear
    if (ret != 1) {
      return TransferResult::kNoDevice;
    }
    ret = i2c_.write(Register::kFifos);
    if (ret != 1) {
      return TransferResult::kNackAddr;
    }

    wait_ns(260);
    i2c_.start();
    wait_ns(260);
    ret = i2c_.write(kI2cAddr | 0x01);  // ensure read bit is set
    // if (ret != 1) {
    //   return TransferResult::kNoDeviceRead;
    // }

    uint8_t sofByte;
    sofByte = i2c_.read(true);
    if (sofByte & kRxFifoTokenMask != kRxFifoTokens::kSop) {
      return TransferResult::kUnknownRxStructure;
    }

    // Read out and parse the header
    bufferOut[0] = i2c_.read(true);
    bufferOut[1] = i2c_.read(true);
    uint16_t header = UsbPd::unpackUint16(bufferOut + 0);
    uint16_t numDataObjects = UsbPd::extractBits(header,
        UsbPdFormat::MessageHeader::kSizeNumDataObjects, UsbPdFormat::MessageHeader::kPosNumDataObjects);
    bufferInd += 2;

    // Read out additional data objects
    for (uint8_t i=0; i<numDataObjects * 4; i++) {
        bufferOut[bufferInd++] = i2c_.read(true);
    }

    // Drop the CRC bytes, these should be checked by the chip
    for (uint8_t i=0; i<3; i++) {
        i2c_.read(true);
    }
    i2c_.read(false);

    wait_ns(260);
    i2c_.stop();
    return 0;
  }

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
  const uint8_t kRxFifoTokenMask = 0xe0;

  static constexpr kFifoTokens kSopSet[4] = {
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
