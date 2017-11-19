/*
 * can_buffer_timestamp.h
 *
 */

#ifndef __CAN_BUFFER_TIMESTAMP_H__
#define __CAN_BUFFER_TIMESTAMP_H__

#include <mbed.h>
#include <can_buffer.h>
#include "LongTimer.h"

enum ErrID{
    EwIRQ,   // Error Warning
    DoIRQ,   // Data Overflow
    WuIRQ,   // Wake Up
    EpIRQ,   // Error Passive
    AlIRQ,   // Arbitration Lost
    BeIRQ    // Bus Error
};

/** CANMessage struct that also holds a timestamp with date and time w/ microsecond recolution */
struct Timestamped_CANMessage {
  bool isError;
  uint32_t millis;
  union DataUnion {
    ErrID errId;
    CANMessage msg;
    DataUnion(ErrID errId) : errId(errId) {
    }
    DataUnion() {
    }
  } data;

  Timestamped_CANMessage(bool isError, uint32_t millis) :
      isError(isError), millis(millis), data {EwIRQ} {
  }
  Timestamped_CANMessage(bool isError, uint32_t millis, ErrID inErrId) :
      isError(isError), millis(millis), data(inErrId) {
  }
  Timestamped_CANMessage() {
  }
};

/** Templated class with a buffer size based on RXSize and TXSize */
template <int RXSize, int TXSize>
class CANRXTX_Time_Buffer {
public:

  /** Constructs a new, empty CAN message buffer
   *
   *  @param can CAN interface to read messages from
   *  @param dt Reference to a datetime object that it will read the time from
   *  @param t Timer to read the microseconds from, it should be reset every second
   *  @param handle message filter handle (0 for any message)
   */
  CANRXTX_Time_Buffer(CAN& can, LongTimer& t,int handle=0): can(can), handle(handle), timer(t) {
    can.attach(this, &CANRXTX_Time_Buffer<RXSize, TXSize>::handleIrq, CAN::RxIrq);
    can.attach(this, &CANRXTX_Time_Buffer<RXSize, TXSize>::handleIrq, CAN::TxIrq);
    can.attach(this, &CANRXTX_Time_Buffer<RXSize, TXSize>::handle_EWIRQ, CAN::EwIrq);
    can.attach(this, &CANRXTX_Time_Buffer<RXSize, TXSize>::handle_DOIRQ, CAN::DoIrq);
    can.attach(this, &CANRXTX_Time_Buffer<RXSize, TXSize>::handle_EPIRQ, CAN::EpIrq);
    can.attach(this, &CANRXTX_Time_Buffer<RXSize, TXSize>::handle_ALIRQ, CAN::AlIrq);
    can.attach(this, &CANRXTX_Time_Buffer<RXSize, TXSize>::handle_BEIRQ, CAN::BeIrq);
  }

  /** Check if the receive buffer is empty
   *
   *  @returns
   *    true if empty
   *    false if not empty
   */
  bool rxEmpty() const {
    return rxBuffer.empty();
  }

  /** Check if the transmit buffer is empty
   *
   *  @returns
   *    true if empty
   *    false if not empty
   */
  bool txEmpty() const {
    return txBuffer.empty();
  }

  /** Check if the receive buffer is full
   *
   *  @returns
   *    true if full
   *    false if not full
   */
  bool rxFull() const {
    return rxBuffer.full();
  }

  /** Check if the transmit buffer is full
   *
   *  @returns
   *    true if full
   *    false if not full
   */
  bool txFull() const {
    return txBuffer.full();
  }

  /** Read a Timestamped_CANMessage from the buffer.
   *
   *  @param msg A Timestampled_CANMessage to read to.
   *
   *  @returns
   *    0 if no message arrived,
   *    1 if message arrived
   */
  int read(Timestamped_CANMessage& msg) {
    if (!rxEmpty()) {
      msg = rxBuffer.read();
      return 1;
    } else {
      return 0;
    }
  }

  /** Write a Timestamped_CANMessage to the buffer.
   *
   *  @param msg A Timestamped_CANMessage to write.
   *
   *  @returns
   *    0 if no space in buffer,
   *    1 if message added to buffer
   */
  int write(CANMessage msg) {
    if (txEmpty() && (can.txstatus() == CAN::Idle)) {
      return can.write(msg);
    } else if (!txFull()) {
      txBuffer.write(msg);
      return 1;
    } else {
      return 0;
    }
  }

  /**
   * Clear TX send buffer.
   */
  void clearTX() {
    txBuffer.clear();
  }

  /** CAN receive/transmit message IRQ handler
   *  Reads any pending CAN messages into the RX buffer
   *  Stops when there are no more pending messages or the RX buffer is full
   *  If the CAN interface is idle, transmits a message from the TX buffer
   */
  void handleIrq() {
    Timestamped_CANMessage msg(false, timer.read_ms());
    while (can.read(msg.data.msg, handle) && !rxFull()) {
      rxBuffer.write(msg);
    }

    while (!txEmpty()) {
      if (can.write(txBuffer.peek()) != 0) {
        txBuffer.discard();
      } else {
        break;
      }
    }
  }

  /** CAN Error Warning Interrupt handler
   * If a CAN error triggers, a special message indicating an error is added to the buffer
   */
  void handle_EWIRQ() {
    rxBuffer.write(Timestamped_CANMessage(true, timer.read_ms(), EwIRQ));
  }
  void handle_DOIRQ() {
    rxBuffer.write(Timestamped_CANMessage(true, timer.read_ms(), DoIRQ));
  }
  void handle_EPIRQ() {
    rxBuffer.write(Timestamped_CANMessage(true, timer.read_ms(), EpIRQ));
  }
  void handle_ALIRQ() {
    rxBuffer.write(Timestamped_CANMessage(true, timer.read_ms(), AlIRQ));
  }
  void handle_BEIRQ() {
    rxBuffer.write(Timestamped_CANMessage(true, timer.read_ms(), BeIRQ));
  }


private:
  CircularBuffer<Timestamped_CANMessage, RXSize> rxBuffer;    // Circular RX Buffer of Timestamped CAN Messages
  CircularBuffer<CANMessage, TXSize> txBuffer;                // Circular TX Buffer of Regular CAN Messages
  CAN& can;                                                   // CAN object for receiving/transmitting messages
  LongTimer& timer;                                           // Reference to a timer object for timestamping
  const int handle;
};
#endif
