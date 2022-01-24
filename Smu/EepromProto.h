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
  // Returns the size of the proto structure, or 0 if it failed to decode
  size_t readFromEeeprom() {
    EEPROM::init();
    EEPROM::read(kEepromAddr_, this->bytes_, sizeof(this->bytes_));
    return this->decode() ? (this->bytes_[0] + 1) : 0;
  }

  // Encodes the proto and writes it to EEPROM
  // Returns the size of the proto structure, or 0 if it failed to encode
  size_t writeToEeprom() {
    bool success = this->encode() != NULL;
    if (success) {
      EEPROM::init();
      // TODO support proto > 127 bytes and do a proper varint encoding
      EEPROM::write(kEepromAddr_, this->bytes_, this->bytes_[0] + 1);
      return this->bytes_[0] + 1;
    } else {
      return 0;
    }
  }

protected:
  const uint32_t kEepromAddr_ = 0;
};

#endif
