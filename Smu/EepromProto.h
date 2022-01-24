#include "ProtoCoder.h"
#include "EEPROM.h"

#ifndef __EEPROM_PROTO_H__
#define __EEPROM_PROTO_H__

// Wrapper class for a EEPROM-based proto configuration.
template<typename ProtoStruct, size_t BufferSize>
class EepromProto : public BufferedProtoCoder<ProtoStruct, BufferSize> {
public:
  EepromProto(const pb_msgdesc_t &fields) : BufferedProtoCoder<ProtoStruct, BufferSize>(fields, true, false) {
  }

  // Read the EEPROM and decode the proto
  bool readFromEeeprom() {
    EEPROM::init();
    EEPROM::read(kEepromAddr_, this->bytes_, sizeof(this->bytes_));
    return this->decode();
  }

  // Encodes the proto and writes it to EEPROM
  bool writeToEeprom() {
    bool success = this->encode() != NULL;
    if (success) {
      EEPROM::init();
      // TODO support proto > 127 bytes and do a proper varint encoding
      EEPROM::write(kEepromAddr_, this->bytes_, this->bytes_[0] + 1);
    }
    return success;
  }

protected:
  const uint32_t kEepromAddr_ = 0;
};

#endif
