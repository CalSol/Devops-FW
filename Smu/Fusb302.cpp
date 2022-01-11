#include <mbed.h>

#include "UsbPd.h"
#include "Fusb302.h"


int Fusb302::writeRegister(uint8_t addr, uint8_t data) {
  uint8_t payload[1];
  payload[0] = data;
  return writeRegister(addr, 1, payload);
}

int Fusb302::writeRegister(uint8_t addr, size_t len, uint8_t data[]) {
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

int Fusb302::readRegister(uint8_t addr, size_t len, uint8_t data[]) {
  int status = i2c_.write(kI2cAddr, (const char*)&addr, 1, true);
  if (status == 0) {
    status = i2c_.read(kI2cAddr, (char*)data, len);
    return status;
  } else {
    return status;
  }
}

int Fusb302::readRegister(uint8_t addr, uint8_t& dataOut) {
  return readRegister(addr, 1, &dataOut);
}

int Fusb302::readId(uint8_t& idOut) {
  return readRegister(Register::kDeviceId, idOut);
}

int Fusb302::writeFifoMessage(uint16_t header, uint8_t numDataObjects, uint32_t data[]) {
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

int Fusb302::readNextRxFifo(uint8_t bufferOut[]) {
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
  if ((sofByte & kRxFifoTokenMask) != kRxFifoTokens::kSop) {
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
