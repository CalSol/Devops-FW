#include <stdint.h>

#ifndef __USB_PD_H__
#define __USB_PD_H__


namespace UsbPdFormat {
  namespace MessageHeader {
    const uint8_t kSizeExtended = 1;
    const uint8_t kSizeNumDataObjects = 3;
    const uint8_t kSizeMessageId = 3;
    const uint8_t kSizePowerRole = 1;
    const uint8_t kSizeSpecRevision = 2;
    const uint8_t kSizeDataRole = 1;
    const uint8_t kSizeMessageType = 5;
    enum Position {
      kPosMessageType = 0,
      kPosDataRole = 5,
      kPosSpecRevision = 6,
      kPosPowerRole = 8,
      kPosMessageId = 9,
      kPosNumDataObjects = 12,
      kPosExtended = 15,
    };
  }
  const uint8_t kMaxDataObjects = 8;  // artifact of NumDataObjects size
}

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
    kDfp = 1,
  };
  enum SpecificationRevision {
    kRevision1_0 = 0,
    kRevision2_0 = 1,
    kRevision3_0 = 2,
    kRevisionReserved = 3,
  };

  // Masks an input value to the specified number of bits, the shifts it.
  // Used as a component in packing messages.
  static constexpr inline uint32_t maskAndShift(uint32_t data, uint8_t numMaskBits, uint8_t shiftBits) {
    return (data & ((1 << numMaskBits) - 1)) << shiftBits;
  }

  static constexpr inline uint32_t extractBits(uint32_t data, uint8_t numMaskBits, uint8_t shiftBits) {
    return (data >> shiftBits) & ((1 << numMaskBits) - 1);
  }

  // Deserialize little-endian buffer bytes into an uint16
  static constexpr inline uint16_t unpackUint16(uint8_t buffer[]) {
    return (uint16_t)buffer[0] | 
        ((uint16_t)buffer[1] << 8);
  }

  // Deserialize little-endian buffer bytes into an uint32
  static constexpr inline uint32_t unpackUint32(uint8_t buffer[]) {
    return (uint32_t)buffer[0] | 
        ((uint32_t)buffer[1] << 8) |
        ((uint32_t)buffer[2] << 16) |
        ((uint32_t)buffer[3] << 24);
  }

  // Serialize a uint16 into little-endian order
  static constexpr inline void packUint16(uint16_t data, uint8_t buffer[]) {
    buffer[0] = data & 0xff;
    buffer[1] = (data >> 8) & 0xff;
  }

  // Serialize a uint32 into little-endian order
  static constexpr inline void packUint32(uint32_t data, uint8_t buffer[]) {
    buffer[0] = data & 0xff;
    buffer[1] = (data >> 8) & 0xff;
    buffer[2] = (data >> 16) & 0xff;
    buffer[3] = (data >> 24) & 0xff;
  }

  static uint16_t makeHeader(uint8_t messageType, uint8_t numDataObjects, uint8_t messageId, 
      PortPowerRole powerRole = PortPowerRole::kSink, PortDataRole dataRole = PortDataRole::kUfp,
      SpecificationRevision spec = SpecificationRevision::kRevision2_0) {
    return maskAndShift(numDataObjects, UsbPdFormat::MessageHeader::kSizeNumDataObjects, UsbPdFormat::MessageHeader::kPosNumDataObjects) |
        maskAndShift(messageId, UsbPdFormat::MessageHeader::kSizeMessageId, UsbPdFormat::MessageHeader::kPosMessageId) |
        maskAndShift(powerRole, UsbPdFormat::MessageHeader::kSizePowerRole, UsbPdFormat::MessageHeader::kPosPowerRole) |
        maskAndShift(spec, UsbPdFormat::MessageHeader::kSizeSpecRevision, UsbPdFormat::MessageHeader::kPosSpecRevision) |
        maskAndShift(dataRole, UsbPdFormat::MessageHeader::kSizeDataRole, UsbPdFormat::MessageHeader::kPosDataRole) |
        maskAndShift(messageType, UsbPdFormat::MessageHeader::kSizeMessageType, UsbPdFormat::MessageHeader::kPosMessageType);
  }

  struct Capability {  // decoded capabilities message
    enum CapabilityType {
      kFixedSupply = 0,
      kBattery = 1,
      kVariable = 2,
      kAugmented = 3,  // different data format
    };
    CapabilityType capabilitiesType;
    bool dualRolePower;
    bool usbSuspendSupported;
    bool unconstrainedPower;
    bool usbCommunicationsCapable;
    bool dualRoleData;
    bool unchunkedExtendedMessagesSupported;
    uint8_t peakCurrent;  // TODO: not decoded
    uint16_t voltageMv;
    uint16_t maxCurrentMa;
  };

  static Capability unpackCapability(uint32_t packed) {
    Capability capability;
    capability.capabilitiesType = (Capability::CapabilityType)extractBits(packed, 2, 30);
    capability.dualRolePower = extractBits(packed, 1, 29);
    capability.usbSuspendSupported = extractBits(packed, 1, 28);
    capability.unconstrainedPower = extractBits(packed, 1, 27);
    capability.usbCommunicationsCapable = extractBits(packed, 1, 26);
    capability.dualRoleData = extractBits(packed, 1, 25);
    capability.unchunkedExtendedMessagesSupported = extractBits(packed, 1, 24);
    capability.peakCurrent = extractBits(packed, 2, 20);
    capability.voltageMv = extractBits(packed, 10, 10) * 50;
    capability.maxCurrentMa = extractBits(packed, 10, 0) * 10;
    return capability;
  }
};

namespace UsbPdTiming {
  const int tTypeCSendSourceCapMsMax = 200;
  const int tSenderResponseMsMax = 30;  // response GoodCRC EOP to actual response message EOP
  const int tReceiveMs = 1; // actually 0.9-1.1ms, Message EOP to GoodCRC EOP
  const int tSinkRequestMs = 100;  // Wait EOP to earliest the next Request should be sent
}

#endif  // __USB_PD_H__