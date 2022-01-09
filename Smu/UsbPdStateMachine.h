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

  enum ConnectionState {
    kNotConnected,
    kConnectedCc1,
    kConnectedCc2,
  };

  void update() {
    switch (state_) {
      case kStart:
      default:
        if (!init() && !setMeasure(1)) {
          debugInfo("UsbPdStateMachine::update(): Start -> DetectCc1");
          timer_.reset();
          state_ = kDetectCc1;
        } else {
          debugWarn("UsbPdStateMachine::update(): Start init / setMeasure failed")
        }
        break;
      case kDetectCc1:
        if (timer_.read_ms() >= kMeasureTimeMs) {
          if (!readMeasure(cc1MeasureLevel_) && !setMeasure(2)) {
            // No debug statement here, since it rapidly alternates CC1 and CC2
            timer_.reset();
            state_ = kDetectCc2;
          } else {
            debugWarn("UsbPdStateMachine::update(): DetectCc1 readMeasure / setMeasure failed")
          }
        }
        break;
      case kDetectCc2:
        if (timer_.read_ms() >= kMeasureTimeMs) {
          uint8_t cc2MeasureLevel;
          if (!readMeasure(cc2MeasureLevel)) {
            uint8_t setCcPin = 0;  // 0 means none
            if (cc1MeasureLevel_ > 0 && cc1MeasureLevel_ > cc2MeasureLevel) {
              setCcPin = 1;
            } else if (cc2MeasureLevel > 0 && cc2MeasureLevel > cc1MeasureLevel_) {
              setCcPin = 2;
            }

            if (setCcPin == 1 || setCcPin == 2) {
              if (!enablePdTrasceiver(setCcPin)) {
                debugInfo("UsbPdStateMachine::update(): DetectCc2 -> Connected (CC=%i)", setCcPin);
                state_ = kConnected;
                connectionState_ = kConnectedCc2;
              } else {
                debugWarn("UsbPdStateMachine::update(): DetectCc2 enablePdTransceiver failed")
              }
            } else {
              if (!setMeasure(1)) {
                // No debug statement here, since it rapidly alternates CC1 and CC2
                timer_.reset();
                state_ = kDetectCc1;
              } else {
                debugWarn("UsbPdStateMachine::update(): DetectCc2 setMeasure(1) failed")
              }
            }
          } else {
            debugWarn("UsbPdStateMachine::update(): DetectCc2 readMeasure failed")
          }
        }
        break;
      case kConnected:
        if (!int_) {
          int ret;
          uint8_t intVal;
          if (!(ret = fusb_.readRegister(Fusb302::Register::kInterrupt, intVal))) {
            if (intVal & Fusb302::kInterrupt::kICrcChk) {
              debugInfo("UsbPdStateMachine::update(): Connected: ICrcChk");
              processRxMessages();
            }
            if (intVal & Fusb302::kInterrupt::kIBcLvl) {
              debugInfo("UsbPdStateMachine::update(): Connected: IBcLvl");

              uint8_t ccMeasureLevel;
              if (!readMeasure(ccMeasureLevel)) {
                if (ccMeasureLevel == 0) {
                  debugInfo("UsbPdStateMachine::update(): Connected -> (reset)");
                  reset();
                }
              } else {
                debugWarn("UsbPdStateMachine::update(): Connected: IBcLvl readMeasure failed")
              }
            }
          } else {
            debugWarn("UsbPdStateMachine::update(): Connected readRegister(Interrupt) failed")
          }
        }
        if (sourceCapabilitiesLen_ == 0) {
          int ret;
          uint16_t header = UsbPd::makeHeader(UsbPd::ControlMessageType::kGetSourceCap, 0, nextMessageId_);
          if (!(ret = fusb_.writeFifoMessage(header))) {
            debugWarn("UsbPdStateMachine::update(): Connected writeFifoMessage(GetSourceCap, %i) failed", nextMessageId_)
          }
          nextMessageId_ = (nextMessageId_ + 1) % 8;
        }
        break;
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

  ConnectionState getConnectionState() const {
    return connectionState_;
  }

  uint16_t errorCount_ = 0;

protected:
  void reset() {
    state_ = kStart;
    deviceIdValid_ = false;
    connectionState_ = kNotConnected;
    nextMessageId_ = 0;

    sourceCapabilitiesLen_ = 0;
  }

  // Resets and initializes the FUSB302 from an unknown state
  int init() {
    int ret;

    if ((ret = fusb_.writeRegister(Fusb302::Register::kReset, 0x01))) {  // reset everything
      debugWarn("UsbPdStateMachine::init(): reset failed = %i", ret);
      errorCount_ ++; return ret;
    }
    wait_ns(Fusb302::kStopStartDelayNs);

    if ((ret = fusb_.readId(deviceId_))) {
      debugWarn("UsbPdStateMachine::init(): device ID failed = %i", ret);
      errorCount_ ++; return ret;
    }
    deviceIdValid_ = true;
    wait_ns(Fusb302::kStopStartDelayNs);

    if ((ret = fusb_.writeRegister(Fusb302::Register::kPower, 0x0f))) {  // power up everything
      debugWarn("UsbPdStateMachine::init(): powerr failed = %i", ret);
      errorCount_ ++; return ret;
    }
    wait_ns(Fusb302::kStopStartDelayNs);  
    if ((ret = fusb_.writeRegister(Fusb302::Register::kControl0, 0x04))) {  // unmask interrupts
      debugWarn("UsbPdStateMachine::init(): control0 failed = %i", ret);
      errorCount_ ++; return ret;
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

    if ((ret = fusb_.writeRegister(Fusb302::Register::kSwitches0, switches0Val))) {
      debugWarn("UsbPdStateMachine::enablePdTransceiver(): switches0 failed = %i", ret);
      errorCount_ ++; return ret;
    }
    wait_ns(Fusb302::kStopStartDelayNs);
    if ((ret = fusb_.writeRegister(Fusb302::Register::kSwitches1, switches1Val))) {
      debugWarn("UsbPdStateMachine::enablePdTransceiver(): switches1 failed = %i", ret);
      errorCount_ ++; return ret;
    }
    wait_ns(Fusb302::kStopStartDelayNs);
    if ((ret = fusb_.writeRegister(Fusb302::Register::kControl3, 0x07))) {  // enable auto-retry
    debugWarn("UsbPdStateMachine::enablePdTransceiver(): control3 failed = %i", ret);
      errorCount_ ++; return ret;
    }
    wait_ns(Fusb302::kStopStartDelayNs);

    if ((ret = fusb_.writeRegister(Fusb302::Register::kReset, 0x02))) {  // reset PD logic
      debugWarn("UsbPdStateMachine::enablePdTransceiver(): reset failed = %i", ret);
      errorCount_ ++; return ret;
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
      debugWarn("UsbPdStateMachine::setMeasure(): invalid ccPin arg = %i", ccPin);
      return -1;  // TODO better error codes
    }
    if ((ret = fusb_.writeRegister(Fusb302::Register::kSwitches0, switches0Val))) {
      debugWarn("UsbPdStateMachine::setMeasure(): switches0 failed = %i", ret);
      errorCount_ ++; return ret;
    }
    wait_ns(Fusb302::kStopStartDelayNs);

    return 0;
  }

  int readMeasure(uint8_t& result) {
    uint8_t regVal;
    int ret;
    if ((ret = fusb_.readRegister(Fusb302::Register::kStatus0, regVal))) {
      debugWarn("UsbPdStateMachine::readMeasure(): status0 failed = %i", ret);
      errorCount_ ++; return ret;
    }
    wait_ns(Fusb302::kStopStartDelayNs);

    result = regVal & 0x03;  // take BC_LVL only
    return 0;
  }

  int processRxMessages() {
    while (true) {
      int ret;
      uint8_t status1Val;
      if ((ret = fusb_.readRegister(Fusb302::Register::kStatus1, status1Val))) {
        debugWarn("UsbPdStateMachine::processRxMessages(): readRegister(Status1) failed = %i", ret);
        return -1;  // exit on error condition
      }
      wait_ns(Fusb302::kStopStartDelayNs);

      bool rxEmpty = status1Val & Fusb302::kStatus1::kRxEmpty;
      if (rxEmpty) {
        return 0;
      }

      uint8_t rxData[30];
      if ((ret = fusb_.readNextRxFifo(rxData))) {
        debugWarn("UsbPdStateMachine::processRxMessages(): readNextRxFifo failed = %i", ret);
        return -1;  // exit on error condition
      }
      uint16_t header = UsbPd::unpackUint16(rxData + 0);
      uint8_t messageType = UsbPd::extractBits(header,
          UsbPdFormat::MessageHeader::kSizeMessageType, UsbPdFormat::MessageHeader::Position::kPosMessageType);
      uint8_t messageId = UsbPd::extractBits(header,
          UsbPdFormat::MessageHeader::kSizeMessageId, UsbPdFormat::MessageHeader::Position::kPosMessageId);
      uint8_t messageNumDataObjects = UsbPd::extractBits(header,
          UsbPdFormat::MessageHeader::kSizeNumDataObjects, UsbPdFormat::MessageHeader::Position::kPosNumDataObjects);
      if (messageNumDataObjects > 0) {  // data message
        debugInfo("UsbPdStateMachine::processRxMessages():  data message: id=%i, type=%03x, numData=%i", 
            messageId, messageType, messageNumDataObjects);
        switch (messageType) {
          case UsbPd::DataMessageType::kSourceCapabilities:
            processRxSourceCapabilities(messageNumDataObjects, rxData);
            break;
          default:  // ignore
            break;
        }
      } else {  // command message
        debugInfo("UsbPdStateMachine::processRxMessages():  command message: id=%i, type=%03x", 
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
    }
    sourceCapabilitiesLen_ = numDataObjects;
  }

  enum UsbPdState {
    kStart,  // FSM starts here, before FUSB302 is initialized
    kDetectCc1,  // FUSB measuring the CC1 pin
    kDetectCc2,  // FUSB measuring the CC2 pin
    kConnected,  // Connected, ready to accept commands
  };
  UsbPdState state_ = kStart;

  // FUSB interface state
  bool deviceIdValid_ = false;
  uint8_t deviceId_;

  // CC detection state
  uint8_t cc1MeasureLevel_;  // written to save the kDetectCc1 measurement state
  ConnectionState connectionState_ = kNotConnected;

  // USB PD state
  uint8_t nextMessageId_;

  int8_t sourceCapabilitiesLen_ = 0;
  uint32_t sourceCapabilitiesObjects_[UsbPdFormat::kMaxDataObjects];

  Fusb302& fusb_;
  DigitalIn& int_;
  Timer timer_;

  static const int kMeasureTimeMs = 10;  // TODO arbitrary
};

#endif
