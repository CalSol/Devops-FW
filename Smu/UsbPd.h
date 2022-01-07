#include <stdint.h>

#ifndef __USB_PD_H__
#define __USB_PD_H__


class UsbPd {
public:
  enum ControlMessageType {
    kControlReserved = 0x00,
    kGoodCrc = 0x01,
    kGotoMin = 0x02,
    kAccept = 0x03,
    kReject = 0x04,
    kPsRdy = 0x06,
    kGetSourceCap = 0x07,
    kGetSinkCap = 0x08,
    kGetStatus = 0x12,
    kGetCountryCodes = 0x15,
  };

  enum DataMessageType {
    kDataReserved = 0x00,
    kSourceCapabilities = 0x01,
    kRequest = 0x02,
    kSinkCapabilities = 0x04,
    kGetCountryInfo = 0x07,
  };

  enum PortPowerRole {
    kSink = 0,
    kSource = 1,
  };
  enum PortDataRole {
    kUfp = 0,
    kDsp = 1,
  };
  enum SpecificationRevision {
    kRevision1_0 = 0,
    kRevision2_0 = 1,
    kRevision3_0 = 2,
    kRevisionReserved = 3,
  };

  // Masks an input value to the specified number of bits, the shifts it.
  // Used as a component in packing messages.
  static constexpr inline uint16_t maskAndShift(uint16_t data, uint8_t maskBits, uint8_t shiftBits) {
    return data & ((1 << maskBits) - 1) << shiftBits;
  }

  static uint16_t makeHeader(ControlMessageType messageType, uint8_t numDataObjects, uint8_t messageId, 
      PortPowerRole powerRole = PortPowerRole::kSink, PortDataRole dataRole = PortDataRole::kUfp,
      SpecificationRevision spec = SpecificationRevision::kRevision3_0) {
    return maskAndShift(numDataObjects, 3, 12) |
        maskAndShift(messageId, 3, 9) |
        maskAndShift(powerRole, 1, 8) |
        maskAndShift(spec, 2, 6) |
        maskAndShift(dataRole, 1, 5) |
        maskAndShift(messageType, 3, 0);
  }
};

#endif  // __USB_PD_H__
