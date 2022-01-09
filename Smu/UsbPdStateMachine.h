#include <mbed.h>

#define DEBUG_ENABLED
#include "debug.h"

#include "Fusb302.h"
#include "UsbPd.h"


#ifndef __USB_PD_STATE_MACHINE_H__
#define __USB_PD_STATE_MACHINE_H__

/**
 * A USB PD state machine using the FUSB302 chip, designed for use in a cooperatively multitasked (non-RTOS)
 * system and instantiated as an object in top-level code
 */
class UsbPdStateMachine {
public:
  UsbPdStateMachine(Fusb302& fusb, DigitalIn& interrupt) : fusb_(fusb), int_(interrupt) {
    timer_.start();
  }

  void update() {
    if (compLowTimer_.read_ms() >= kCompLowResetTimeMs) {
      debugWarn("update(): Comp low reset");
      reset();
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
          debugInfo("update(): EnableTransceiver -> Connected");
          timer_.reset();
          state_ = kWaitSourceCapabilities;
        } else {
          debugWarn("update(): EnableTransceiver enablePdTransceiver failed");
        }
        break;
      case kWaitSourceCapabilities:
        if (!int_) {
          processInterrupt();
        }
        if (sourceCapabilitiesLen_ > 0) {
          state_ = kConnected;
          debugInfo("update(): WaitSourceCapabilities -> Connected");
        } else if (timer_.read_ms() > UsbPdTiming::tTypeCSendSourceCapMsMax) {
          state_ = kEnableTransceiver;
          debugInfo("update(): WaitSourceCapabilities -> EnableTransceiver");
        }
        break;
      case kConnected:
        if (!int_) {
          processInterrupt();
        }
        break;
    }
  }

  void processInterrupt() {
    int ret;
    uint8_t intVal[2];
    ret = fusb_.readRegister(Fusb302::Register::kInterrupt, intVal[0]);
    wait_ns(Fusb302::kStopStartDelayNs);
    if (!ret) {
      if (intVal[0] & Fusb302::kInterrupt::kICrcChk) {
        debugInfo("update(): Connected: ICrcChk");
        processRxMessages();
      }
      if (intVal[0] & Fusb302::kInterrupt::kICompChng) {
        debugInfo("update(): Connected: ICompChng");
        uint8_t compResult;
        if (!readComp(compResult)) {
          if (compResult == 0) {
            compLowTimer_.start();
          } else {
            compLowTimer_.reset();
            compLowTimer_.stop();
          }
        } else {
          debugWarn("update(): Connected: ICompChng readComp failed");
        }
        wait_ns(Fusb302::kStopStartDelayNs);
      }
    } else {
      debugWarn("update(): Connected readRegister(Interrupt) failed");
    }
  }

  // Returns the device ID read on initialization, or -1 if not read yet.
  int getDeviceId() const {
    if (!deviceIdValid_) {
      return -1;
    } else {
      return deviceId_;
    }
  }

  // Gets the capabilities of the source. Returns the total count, and the unpacked capabilities are
  // stored in the input array.
  // Zero means no source is connected, or capabilities are not yet available.
  // Can return up to 8 objects.
  int getCapabilities(UsbPd::Capability capabilities[]) {
    for (uint8_t i=0; i<sourceCapabilitiesLen_; i++) {
      capabilities[i] = UsbPd::unpackCapability(sourceCapabilitiesObjects_[i]);
    }
    return sourceCapabilitiesLen_;
  }

  // The currently active capability requested to and confirmed by the source.
  // Zero means the default (none was requested).
  // 1 is the first capability, consistent with the object position field described in the PD spec.
  uint8_t selectedCapability() {
    return selectedCapability_;
  }

  // Requests a capability from the source.
  // 1 is the first capability, consistent with the object position field described in the PD spec.
  int requestCapability(uint8_t capability, uint16_t currentMa) {
    int ret;
    uint16_t header = UsbPd::makeHeader(UsbPd::DataMessageType::kRequest, 1, nextMessageId_);

    uint32_t requestData = UsbPd::maskAndShift(capability, 3, 28) |
        UsbPd::maskAndShift(1, 10, 24) |  // no USB suspend
        UsbPd::maskAndShift(currentMa / 10, 10, 10) |
        UsbPd::maskAndShift(currentMa / 10, 10, 0);

    if (!(ret = fusb_.writeFifoMessage(header, 1, &requestData))) {
      debugInfo("requestCapability(): writeFifoMessage(Request(%i), %i)", capability, nextMessageId_);
    } else {
      debugWarn("requestCapability(): writeFifoMessage(Request(%i), %i) failed", capability, nextMessageId_);
    }
    nextMessageId_ = (nextMessageId_ + 1) % 8;
    return ret;
  }

  uint16_t errorCount_ = 0;

protected:
  void reset() {
    state_ = kStart;
    deviceIdValid_ = false;
    ccPin_ = 0;
    nextMessageId_ = 0;

    sourceCapabilitiesLen_ = 0;
    selectedCapability_ = 0;

    compLowTimer_.stop();
    compLowTimer_.reset();
  }

  // Resets and initializes the FUSB302 from an unknown state
  int init() {
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
    if ((ret = fusb_.writeRegister(Fusb302::Register::kControl0, 0x04))) {  // unmask interrupts
      debugWarn("init(): control0 failed = %i", ret);
      errorCount_++; return ret;
    }
    wait_ns(Fusb302::kStopStartDelayNs);

    return 0;
  }

  // Enable the transmitter with the given CC pin (1 or 2, anything else is invalid)
  int enablePdTrasceiver(int ccPin) {
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

    if ((ret = fusb_.writeRegister(Fusb302::Register::kReset, 0x02))) {  // reset PD logic
      debugWarn("enablePdTransceiver(): reset failed = %i", ret);
      errorCount_++; return ret;
    }
    wait_ns(Fusb302::kStopStartDelayNs);

    return 0;
  }

  int setMeasure(int ccPin) {
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

  int readMeasure(uint8_t& result) {
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

  int readComp(uint8_t& result) {
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

  int processRxMessages() {
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
      uint8_t messageType = UsbPd::extractBits(header,
          UsbPdFormat::MessageHeader::kSizeMessageType, UsbPdFormat::MessageHeader::Position::kPosMessageType);
      uint8_t messageId = UsbPd::extractBits(header,
          UsbPdFormat::MessageHeader::kSizeMessageId, UsbPdFormat::MessageHeader::Position::kPosMessageId);
      uint8_t messageNumDataObjects = UsbPd::extractBits(header,
          UsbPdFormat::MessageHeader::kSizeNumDataObjects, UsbPdFormat::MessageHeader::Position::kPosNumDataObjects);
      if (messageNumDataObjects > 0) {  // data message
        debugInfo("processRxMessages():  data message: id=%i, type=%03x, numData=%i", 
            messageId, messageType, messageNumDataObjects);
        switch (messageType) {
          case UsbPd::DataMessageType::kSourceCapabilities:
            processRxSourceCapabilities(messageNumDataObjects, rxData);
            break;
          default:  // ignore
            break;
        }
      } else {  // command message
        debugInfo("processRxMessages():  command message: id=%i, type=%03x", 
            messageId, messageType);
        switch (messageType) {
          case UsbPd::ControlMessageType::kGoodCrc:
          default:  // ignore
            break;
        }
      }
    }
  }

  void processRxSourceCapabilities(uint8_t numDataObjects, uint8_t rxData[]) {
    for (uint8_t i=0; i<numDataObjects; i++) {
      sourceCapabilitiesObjects_[i] = UsbPd::unpackUint32(rxData + 2 + 4*i);
      UsbPd::Capability capability = UsbPd::unpackCapability(sourceCapabilitiesObjects_[i]);
      if (capability.capabilitiesType == UsbPd::Capability::CapabilityType::kFixedSupply) {
        debugInfo("processRxSourceCapabilities %i/%i 0x%08lx = Fixed: %i mV, %i mA; %s %s",
            i+1, numDataObjects, sourceCapabilitiesObjects_[i],
            capability.voltageMv, capability.maxCurrentMa,
            capability.dualRolePower ? "DualRolePower" : "NonDualRolePower",
            capability.unconstrainedPower ? "Unconstrained" : "Constrained");
      } else {
        debugInfo("processRxSourceCapabilities %i/%i 0x%08lx = type %i", 
            i+1, numDataObjects, sourceCapabilitiesObjects_[i], 
            capability.capabilitiesType);
      }

    }
    sourceCapabilitiesLen_ = numDataObjects;
  }

  enum UsbPdState {
    kStart,  // FSM starts here, before FUSB302 is initialized
    kDetectCc,  // alternate measuring CC1/CC2
    kEnableTransceiver,  // reset and enable transceiver
    kWaitSourceCapabilities,  // waiting for initial source capabilities message
    kConnected,  // connected, ready to accept commands
  };
  UsbPdState state_ = kStart;

  // FUSB interface state
  bool deviceIdValid_ = false;
  uint8_t deviceId_;

  // CC detection state
  int8_t savedCcMeasureLevel_;  // last measured level of the other CC pin, or -1 if not yet measured
  int8_t measuringCcPin_;  // CC pin currently being measured
  uint8_t ccPin_;  // CC pin used for communication, only valid when connected
  
  // USB PD state
  uint8_t nextMessageId_;

  // only written to from within interrupt handler
  volatile uint8_t sourceCapabilitiesLen_ = 0;
  volatile uint32_t sourceCapabilitiesObjects_[UsbPdFormat::kMaxDataObjects];
  uint8_t selectedCapability_ = 0;

  Fusb302& fusb_;
  DigitalIn& int_;
  Timer timer_;
  Timer compLowTimer_;  // only written from interrupt

  static const int kMeasureTimeMs = 1;  // TODO arbitrary
  static const int kCompLowResetTimeMs = 1000;  // time Vbus needs to be low to detect a disconnect; TODO arbitrary
  static const int kCompVBusThresholdMv = 3000;  // account for leakage from 3.3v
};

#endif
