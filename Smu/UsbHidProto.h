#ifndef __USB_HID_PROTO_H__
#define __USB_HID_PROTO_H__

#include "USBHID.h"
#include "pb_encode.h"
#include "pb_decode.h"


// Wrapper class that presents a protobuf abstraction on top of the USB HID transport.
//
// Handles chunking a message into multiple reports:
// The first report of a proto begins with the varint containing the proto length.
// Successive reports of the same proto begin with a zero.
// On the receiver size, it will keep receiving reports until it receives bytes of the specified length.
// If it receives a zero-header report when it is expecting a new packet, it will be discarded.
// If it receives a varint-header report when it is expecting a continued report, 
//   it will discard the prior reports.
template <typename TransmitStruct, size_t TransmitSize, typename ReceiveStruct, size_t ReceiveSize>
class UsbHidProto : public USBHID {
public:
  UsbHidProto(const pb_msgdesc_t &transmitFields, const pb_msgdesc_t &receiveFields, 
    uint8_t output_report_length = 64, uint8_t input_report_length = 64,
    uint16_t vendor_id = 0x1234, uint16_t product_id = 0x0006, uint16_t product_release = 0x0001, 
    bool connect = true) :
    USBHID(output_report_length, input_report_length, vendor_id, product_id, product_release, connect),
    outputReportLength_(output_report_length),
    transmitFields_(transmitFields), receiveFields_(receiveFields) {
  }

  // Sends a proto (serialized from the argument pointer), returning true if successful
  bool sendProto(TransmitStruct* pb) {
    pb_ostream_t stream = pb_ostream_from_buffer(transmitBuffer_, sizeof(transmitBuffer_));
    if (!pb_encode_ex(&stream, &transmitFields_, pb, PB_ENCODE_DELIMITED)) {
      return false;  // failed to encode
    }
    transmitLength_ = stream.bytes_written;
    transmitSize_ = 0;

    HID_REPORT transmitReport;
    transmitReport.length = outputReportLength_;
    size_t reportDataLength = min((size_t)transmitReport.length, transmitLength_);
    memcpy(transmitReport.data, transmitBuffer_, reportDataLength);
    transmitSize_ += reportDataLength;
    if (!this->send(&transmitReport)) {
      return false;
    }
    
    while (transmitSize_ < transmitLength_) {
      // for each successive report, we need to prepend a zero
      reportDataLength = min((size_t)transmitReport.length - 1, transmitLength_ - transmitSize_);
      transmitReport.data[0] = 0;
      memcpy(transmitReport.data + 1, transmitBuffer_ + transmitSize_, reportDataLength);
      transmitSize_ += reportDataLength;
      if (!this->send(&transmitReport)) {
        return false;
      }
    }
    return true;
  }

  // Reads a proto (deserialized into the argument pointer), returning true of successful
  bool readProtoNb(ReceiveStruct* pb) {
    HID_REPORT receivedReport;
    if (this->readNB(&receivedReport)) {
      if (receivedReport.data[0] == 0) {  // continuation report
        if (receiveLength_ == 0) {
          return false;  // no message in progress, discard
        }

        // discard the zero from the buffer
        size_t reportDataLength = min((size_t)receivedReport.length - 1, receiveLength_ - receiveSize_);
        memcpy(receiveBuffer_ + receiveSize_, receivedReport.data + 1, reportDataLength);
        receiveSize_ += reportDataLength;
      } else {  // new report
        receiveLength_ = 0;  // even if another message was in progress, discard it
        receiveSize_ = 0;
        pb_istream_t stream = pb_istream_from_buffer(receivedReport.data, receivedReport.length);
        uint64_t protoSize;
        if (!pb_decode_varint(&stream, &protoSize)) {
          return false;  // if decode failed for whatever reason, discard
        }
        size_t lengthSize = receivedReport.length - stream.bytes_left;
        if (protoSize <= sizeof(receiveBuffer_)) {  // discard in case of overrun
          receiveLength_ = lengthSize + protoSize;
          // include the size in the buffer, since the proto decoder depends on it
          size_t reportDataLength = min((size_t)receivedReport.length, receiveLength_);
          memcpy(receiveBuffer_, receivedReport.data, reportDataLength);
          receiveSize_ += reportDataLength;
        }
      }
      if (receiveLength_ > 0 && receiveSize_ >= receiveLength_) {  // enough bytes received, try decoding
        // note received size can be over since reports may be fixed size
        pb_istream_t stream = pb_istream_from_buffer(receiveBuffer_, sizeof(receiveBuffer_));
        bool success = pb_decode_ex(&stream, &receiveFields_, pb, PB_DECODE_DELIMITED);
        receiveLength_ = 0;  // clear the buffers for the next message
        receiveSize_ = 0;
        return success;
      } else {
        return false;  // no full message available
      }
    } else {
      return false;  // no new data received
    }
  }

protected:
  const uint8_t outputReportLength_;  // private in USBHID so duplicated here
  const pb_msgdesc_t &transmitFields_;
  const pb_msgdesc_t &receiveFields_;

  size_t transmitLength_ = 0;  // total length of the transmit buffer, zero for none being transmitted
                               // this should never > TransmitSize
  size_t transmitSize_ = 0;  // index of the next untransmitted byte in the transmit buffer
  uint8_t transmitBuffer_[TransmitSize];  // proto transmit buffer

  size_t receiveLength_ = 0;  // total length of the message being assembled, zero for none being assembled
                              // this should never > ReceiveSize
  size_t receiveSize_ = 0;  // index of the next free byte in the assembly buffer
  uint8_t receiveBuffer_[ReceiveSize];  // proto assembly buffer, for multi-report protos
};

#endif
