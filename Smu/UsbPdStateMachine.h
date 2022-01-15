#include <mbed.h>

#include "UsbPd.h"
#include "Fusb302.h"


#ifndef __USB_PD_STATE_MACHINE_H__
#define __USB_PD_STATE_MACHINE_H__

/**
 * A USB PD state machine using the FUSB302 chip, designed for use in a cooperatively multitasked (non-RTOS)
 * system and instantiated as an object in top-level code
 */
class UsbPdStateMachine {
public:
  UsbPdStateMachine(Fusb302& fusb, InterruptIn& interrupt) : fusb_(fusb), int_(interrupt) {
    timer_.start();
    int_.fall(callback(this, &UsbPdStateMachine::actualInterrupt));
  }

  // Updates the state machine, handling non-time-sensitive operations.
  // This needs to be called regularly.
  void update();

  void actualInterrupt();

  void processInterrupt();

  // Returns the device ID read on initialization, or -1 if not read yet.
  int getDeviceId() const;

  // Gets the capabilities of the source. Returns the total count, and the unpacked capabilities are
  // stored in the input array.
  // Zero means no source is connected, or capabilities are not yet available.
  // Can return up to 8 objects.
  int getCapabilities(UsbPd::Capability::Unpacked capabilities[]);

  // The currently active capability requested to and confirmed by the source.
  // Zero means the default (none was requested).
  // 1 is the first capability, consistent with the object position field described in the PD spec.
  uint8_t currentCapability();

  // Requests a capability from the source.
  // 
  int requestCapability(uint8_t capability, uint16_t currentMa);

  uint16_t errorCount_ = 0;

protected:
  void reset();

  // Resets and initializes the FUSB302 from an unknown state
  int init();

  // Enable the transmitter with the given CC pin (1 or 2, anything else is invalid)
  int enablePdTrasceiver(int ccPin);

  int setMeasure(int ccPin);

  int readMeasure(uint8_t& result);

  int readComp(uint8_t& result);

  int processRxMessages();

  void processRxSourceCapabilities(uint8_t numDataObjects, uint8_t rxData[]);

  // Requests a capability from the source.
  // 1 is the first capability, consistent with the object position field described in the PD spec.
  int sendRequestCapability(uint8_t capability, uint16_t currentMa);

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

  volatile uint8_t sourceCapabilitiesLen_ = 0;  // written by ISR
  volatile uint32_t sourceCapabilitiesObjects_[UsbPd::MessageHeader::kMaxDataObjects];  // written by ISR

  // 
  uint8_t requestedCapability_;  // currently requested capability
  volatile uint8_t currentCapability_;  // current accepted capability, 0 is default, written by ISR
  bool powerStable_;
  
  Fusb302& fusb_;
  InterruptIn& int_;
  Timer timer_;
  Timer compLowTimer_;  // only written from interrupt

  static const int kMeasureTimeMs = 1;  // TODO arbitrary
  static const int kCompLowResetTimeMs = 50;  // time Vbus needs to be low to detect a disconnect; TODO arbitrary
  static const int kCompVBusThresholdMv = 3000;  // account for leakage from 3.3v
};

#endif
