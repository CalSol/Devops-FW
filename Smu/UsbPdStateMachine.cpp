#include <mbed.h>
#include "UsbPd.h"
#include "UsbPdStateMachine.h"

// #define DEBUG_ENABLED
#include "debug.h"


void UsbPdStateMachine::update() {
  int_.disable_irq();

  if (state_ > kEnableTransceiver) {  // poll to detect COMP VBus low
    uint8_t compResult;
    if (!readComp(compResult)) {
      if (compResult == 0) {
        compLowTimer_.start();
      } else {
        compLowTimer_.reset();
        compLowTimer_.stop();
      }
    } else {
      debugWarn("processInterrupt(): ICompChng readComp failed");
    }
    wait_ns(Fusb302::kStopStartDelayNs);
    if (compLowTimer_.read_ms() >= kCompLowResetTimeMs) {
      debugWarn("update(): Comp low reset");
      reset();
    }
  }

  switch (state_) {
    case kStart:
    default:
      if (!init()) {
        debugInfo("update(): Start -> DetectCc");
        timer_.reset();
        measuringCcPin_ = -1;
        savedCcMeasureLevel_ = -1;
        state_ = kDetectCc;
      } else {
        debugWarn("update(): Start init failed");
      }
      break;
    case kDetectCc:
      if ((measuringCcPin_ == 1 || measuringCcPin_ == 2) && timer_.read_ms() >= kMeasureTimeMs) {  // measurerment ready
        uint8_t measureLevel;
        if (!readMeasure(measureLevel)) {
          if (savedCcMeasureLevel_ != -1 && measureLevel != savedCcMeasureLevel_) {
            // last measurement on other pin was valid, and one is higher
            if (measureLevel > savedCcMeasureLevel_) {  // this measurement higher, use this CC pin
              ccPin_ = measuringCcPin_;
            } else {  // other measurement higher, use other rCC pin
              ccPin_ = measuringCcPin_ == 1 ? 2 : 1;
            }
            state_ = kEnableTransceiver;
            debugInfo("update(): DetectCc -> EnableTransceiver (CC=%i)", ccPin_);
          } else {  // save this measurement and swap measurement pins
            uint8_t nextMeasureCcPin = measuringCcPin_ == 1 ? 2 : 1;
            if (!setMeasure(nextMeasureCcPin)) {
              timer_.reset();
              compLowTimer_.reset();
              compLowTimer_.start();
              savedCcMeasureLevel_ = measureLevel;
              measuringCcPin_ = nextMeasureCcPin;
            } else {
              debugWarn("update(): DetectCc setMeasure failed");
            }
          }
        } else {
          debugWarn("update(): DetectCc readMeasure failed");
        }
      } else if (measuringCcPin_ != 1 && measuringCcPin_ != 2) {  // state entry, invalid measurement pin
        if (!setMeasure(1)) {
          timer_.reset();
          measuringCcPin_ = 1;
        } else {
          debugWarn("update(): DetectCc setMeasure failed");
        }
      }
      break;
    case kEnableTransceiver:
      if (!enablePdTrasceiver(ccPin_)) {
        debugInfo("update(): EnableTransceiver -> WaitSourceCapabilities");
        timer_.reset();
        state_ = kWaitSourceCapabilities;
      } else {
        debugWarn("update(): EnableTransceiver enablePdTransceiver failed");
      }
      break;
    case kWaitSourceCapabilities:
      if (sourceCapabilitiesLen_ > 0) {
        state_ = kConnected;
        debugInfo("update(): WaitSourceCapabilities -> Connected");
      } else if (timer_.read_ms() > UsbPdTiming::tTypeCSendSourceCapMsMax) {
        state_ = kEnableTransceiver;
        debugInfo("update(): WaitSourceCapabilities -> EnableTransceiver");
      }
      break;
    case kConnected:
      break;
  }

  if (!int_) {  // interrupt doesn't trigger reliably
    debugInfo("update() polling interrupt");
    processInterrupt();
  }

  int_.enable_irq();
}

void UsbPdStateMachine::actualInterrupt() {
  debugInfo("actualInterrupt()");  // to distinguish from polling interrupts
  processInterrupt();
}

void UsbPdStateMachine::processInterrupt() {
  int ret;
  uint8_t intVal;
  ret = fusb_.readRegister(Fusb302::Register::kInterrupt, intVal);
  wait_ns(Fusb302::kStopStartDelayNs);
  if (!ret) {
    if (intVal & Fusb302::kInterrupt::kICrcChk) {
      debugInfo("processInterrupt(): ICrcChk");
      processRxMessages();
    }
  } else {
    debugWarn("processInterrupt(): readRegister(Interrupt) failed");
  }
}

int UsbPdStateMachine::getDeviceId() const {
  if (!deviceIdValid_) {
    return -1;
  } else {
    return deviceId_;
  }
}

int UsbPdStateMachine::getCapabilities(UsbPd::Capability::Unpacked capabilities[]) {
  for (uint8_t i=0; i<sourceCapabilitiesLen_; i++) {
    capabilities[i] = UsbPd::Capability::unpack(sourceCapabilitiesObjects_[i]);
  }
  return sourceCapabilitiesLen_;
}

uint8_t UsbPdStateMachine::currentCapability() {
  return currentCapability_;
}

int UsbPdStateMachine::requestCapability(uint8_t capability, uint16_t currentMa) {
  int ret;
  int_.disable_irq();
  ret = sendRequestCapability(capability, currentMa);
  int_.enable_irq();
  return ret;
}

void UsbPdStateMachine::reset() {
  state_ = kStart;
  deviceIdValid_ = false;
  ccPin_ = 0;
  nextMessageId_ = 0;

  sourceCapabilitiesLen_ = 0;

  requestedCapability_ = 0;
  currentCapability_ = 0;
  powerStable_ = false;

  compLowTimer_.stop();
  compLowTimer_.reset();
}

int UsbPdStateMachine::init() {
  int ret;

  if ((ret = fusb_.writeRegister(Fusb302::Register::kReset, 0x03))) {  // reset everything
    debugWarn("init(): reset failed = %i", ret);
    errorCount_++; return ret;
  }
  wait_ns(Fusb302::kStopStartDelayNs);

  if ((ret = fusb_.readId(deviceId_))) {
    debugWarn("init(): device ID failed = %i", ret);
    errorCount_++; return ret;
  }
  deviceIdValid_ = true;
  wait_ns(Fusb302::kStopStartDelayNs);

  if ((ret = fusb_.writeRegister(Fusb302::Register::kPower, 0x0f))) {  // power up everything
    debugWarn("init(): power failed = %i", ret);
    errorCount_++; return ret;
  }
  wait_ns(Fusb302::kStopStartDelayNs);
  if ((ret = fusb_.writeRegister(Fusb302::Register::kMeasure, 0x40 | (kCompVBusThresholdMv/42)))) {  // MEAS_VBUS
    debugWarn("enablePdTransceiver(): Measure failed = %i", ret);
    errorCount_++; return ret;
  }
  wait_ns(Fusb302::kStopStartDelayNs);  

  return 0;
}

int UsbPdStateMachine::enablePdTrasceiver(int ccPin) {
  int ret;
  uint8_t switches0Val = 0x03;  // PDWN1/2
  uint8_t switches1Val = 0x24;  // Revision 2.0, auto-CRC
  if (ccPin == 1) {
    switches0Val |= 0x04;
    switches1Val |= 0x01;
  } else if (ccPin == 2) {
    switches0Val |= 0x08;
    switches1Val |= 0x02;
  } else {
    return -1;  // TODO better error codes
  }

  if ((ret = fusb_.writeRegister(Fusb302::Register::kSwitches0, switches0Val))) {  // PDWN1/2
    debugWarn("enablePdTransceiver(): switches0 failed = %i", ret);
    errorCount_++; return ret;
  }
  wait_ns(Fusb302::kStopStartDelayNs);
  if ((ret = fusb_.writeRegister(Fusb302::Register::kSwitches1, switches1Val))) {
    debugWarn("enablePdTransceiver(): switches1 failed = %i", ret);
    errorCount_++; return ret;
  }
  wait_ns(Fusb302::kStopStartDelayNs);
  if ((ret = fusb_.writeRegister(Fusb302::Register::kControl3, 0x07))) {  // enable auto-retry
  debugWarn("enablePdTransceiver(): control3 failed = %i", ret);
    errorCount_++; return ret;
  }
  wait_ns(Fusb302::kStopStartDelayNs);
  if ((ret = fusb_.writeRegister(Fusb302::Register::kMask, 0xef))) {  // mask interupts
    debugWarn("enablePdTransceiver(): mask failed = %i", ret);
    errorCount_++; return ret;
  }
  wait_ns(Fusb302::kStopStartDelayNs);
  if ((ret = fusb_.writeRegister(Fusb302::Register::kMaska, 0xff))) {  // mask interupts
    debugWarn("enablePdTransceiver(): maska failed = %i", ret);
    errorCount_++; return ret;
  }
  wait_ns(Fusb302::kStopStartDelayNs);
  if ((ret = fusb_.writeRegister(Fusb302::Register::kMaskb, 0x01))) {  // mask interupts
    debugWarn("enablePdTransceiver(): maskb failed = %i", ret);
    errorCount_++; return ret;
  }
  wait_ns(Fusb302::kStopStartDelayNs);
  if ((ret = fusb_.writeRegister(Fusb302::Register::kControl0, 0x04))) {  // unmask global interrupt
    debugWarn("enablePdTransceiver(): control0 failed = %i", ret);
    errorCount_++; return ret;
  }
  wait_ns(Fusb302::kStopStartDelayNs);

  if ((ret = fusb_.writeRegister(Fusb302::Register::kReset, 0x02))) {  // reset PD logic
    debugWarn("enablePdTransceiver(): reset failed = %i", ret);
    errorCount_++; return ret;
  }
  wait_ns(Fusb302::kStopStartDelayNs);

  return 0;
}

int UsbPdStateMachine::setMeasure(int ccPin) {
  int ret;
  uint8_t switches0Val = 0x03;  // PDWN1/2
  if (ccPin == 1) {
    switches0Val |= 0x04;
  } else if (ccPin == 2) {
    switches0Val |= 0x08;
  } else {
    debugWarn("setMeasure(): invalid ccPin arg = %i", ccPin);
    return -1;  // TODO better error codes
  }
  if ((ret = fusb_.writeRegister(Fusb302::Register::kSwitches0, switches0Val))) {
    debugWarn("setMeasure(): switches0 failed = %i", ret);
    errorCount_++; return ret;
  }
  wait_ns(Fusb302::kStopStartDelayNs);

  return 0;
}

int UsbPdStateMachine::readMeasure(uint8_t& result) {
  uint8_t regVal;
  int ret;
  if ((ret = fusb_.readRegister(Fusb302::Register::kStatus0, regVal))) {
    debugWarn("readMeasure(): status0 failed = %i", ret);
    errorCount_++; return ret;
  }
  wait_ns(Fusb302::kStopStartDelayNs);

  result = regVal & 0x03;  // take BC_LVL only
  return 0;
}

int UsbPdStateMachine::readComp(uint8_t& result) {
  uint8_t regVal;
  int ret;
  if ((ret = fusb_.readRegister(Fusb302::Register::kStatus0, regVal))) {
    debugWarn("readMeasure(): status0 failed = %i", ret);
    errorCount_++; return ret;
  }
  wait_ns(Fusb302::kStopStartDelayNs);

  result = regVal & 0x20;  // take COMP only
  return 0;
}

int UsbPdStateMachine::processRxMessages() {
  while (true) {
    int ret;
    uint8_t status1Val;
    if ((ret = fusb_.readRegister(Fusb302::Register::kStatus1, status1Val))) {
      debugWarn("processRxMessages(): readRegister(Status1) failed = %i", ret);
      return -1;  // exit on error condition
    }
    wait_ns(Fusb302::kStopStartDelayNs);

    bool rxEmpty = status1Val & Fusb302::kStatus1::kRxEmpty;
    if (rxEmpty) {
      return 0;
    }

    uint8_t rxData[30];
    if ((ret = fusb_.readNextRxFifo(rxData))) {
      debugWarn("processRxMessages(): readNextRxFifo failed = %i", ret);
      return -1;  // exit on error condition
    }
    wait_ns(Fusb302::kStopStartDelayNs);

    uint16_t header = UsbPd::unpackUint16(rxData + 0);
    uint8_t messageType = UsbPd::MessageHeader::unpackMessageType(header);
    uint8_t messageId = UsbPd::MessageHeader::unpackMessageId(header);
    uint8_t messageNumDataObjects = UsbPd::MessageHeader::unpackNumDataObjects(header);
    if (messageNumDataObjects > 0) {  // data message
      debugInfo("processRxMessages(): data message: id=%i, type=%03x, numData=%i", 
          messageId, messageType, messageNumDataObjects);
      switch (messageType) {
        case UsbPd::MessageHeader::DataType::kSourceCapabilities: {
          bool isFirstMessage = sourceCapabilitiesLen_ == 0;
          processRxSourceCapabilities(messageNumDataObjects, rxData);
          if (isFirstMessage && sourceCapabilitiesLen_ > 0) {
            UsbPd::Capability::Unpacked v5vCapability = UsbPd::Capability::unpack(sourceCapabilitiesObjects_[0]);
            sendRequestCapability(0, v5vCapability.maxCurrentMa);  // request the vSafe5v capability
          } else {
            // TODO this should be an error
          }
        } break;
        default:  // ignore
          break;
      }
    } else {  // command message
      debugInfo("processRxMessages(): command message: id=%i, type=%03x", 
          messageId, messageType);
      switch (messageType) {
        case UsbPd::MessageHeader::ControlType::kAccept:
          currentCapability_ = requestedCapability_;
          break;
        case UsbPd::MessageHeader::ControlType::kReject:
          requestedCapability_ = currentCapability_;
          break;
        case UsbPd::MessageHeader::ControlType::kPsRdy:
          powerStable_ = true;
          debugInfo("processRxMessages(): power ready");
          break;
        case UsbPd::MessageHeader::ControlType::kGoodCrc:
        default:  // ignore
          break;
      }
    }
  }
}

void UsbPdStateMachine::processRxSourceCapabilities(uint8_t numDataObjects, uint8_t rxData[]) {
  for (uint8_t i=0; i<numDataObjects; i++) {
    sourceCapabilitiesObjects_[i] = UsbPd::unpackUint32(rxData + 2 + 4*i);
    UsbPd::Capability::Unpacked capability = UsbPd::Capability::unpack(sourceCapabilitiesObjects_[i]);
    if (capability.capabilitiesType == UsbPd::Capability::Type::kFixedSupply) {
      debugInfo("processRxSourceCapabilities %i/%i 0x%08lx = Fixed: %i mV, %i mA; %s %s",
          i+1, numDataObjects, sourceCapabilitiesObjects_[i],
          capability.voltageMv, capability.maxCurrentMa,
          capability.dualRolePower ? "DRP" : "nDRP",
          capability.unconstrainedPower ? "UC" : "C");
    } else {
      debugInfo("processRxSourceCapabilities %i/%i 0x%08lx = type %i", 
          i+1, numDataObjects, sourceCapabilitiesObjects_[i], 
          capability.capabilitiesType);
    }
  }
  sourceCapabilitiesLen_ = numDataObjects;
}

int UsbPdStateMachine::sendRequestCapability(uint8_t capability, uint16_t currentMa) {
  int ret;
  uint16_t header = UsbPd::MessageHeader::pack(UsbPd::MessageHeader::DataType::kRequest, 1, nextMessageId_);

  uint32_t requestData = UsbPd::maskAndShift(capability, 3, 28) |
      UsbPd::maskAndShift(1, 10, 24) |  // no USB suspend
      UsbPd::maskAndShift(currentMa / 10, 10, 10) |
      UsbPd::maskAndShift(currentMa / 10, 10, 0);
  if (!(ret = fusb_.writeFifoMessage(header, 1, &requestData))) {
    requestedCapability_ = capability;
    powerStable_ = false;
    debugInfo("requestCapability(): writeFifoMessage(Request(%i), %i)", capability, nextMessageId_);
  } else {
    debugWarn("requestCapability(): writeFifoMessage(Request(%i), %i) failed", capability, nextMessageId_);
  }
  nextMessageId_ = (nextMessageId_ + 1) % 8;
  return ret;
}
