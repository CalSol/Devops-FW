#include "RecordEncoding.h"

DataloggerRecord timeToRecord(tm time, uint8_t sourceId, uint32_t timestampMs) {
  DataloggerRecord rtcRec = {
    timestampMs,
    0,
    sourceId,
    DataloggerRecord_rtcTime_tag, {}
  };
  rtcRec.payload.rtcTime = google_protobuf_Timestamp {
    mktime(&time)
  };
  return rtcRec;
}

DataloggerRecord canMessageToRecord(Timestamped_CANMessage msg, uint8_t sourceId) {
  DataloggerRecord rec = {
    msg.millis,
    0,
    sourceId,
    0, {}
  };

  if (!msg.isError) {
    CanMessage msgRec = {
      msg.data.msg.id,
      msg.data.msg.format ? CanMessage_FrameType_EXTENDED_FRAME : CanMessage_FrameType_STANDARD_FRAME,
      msg.data.msg.type ? CanMessage_RtrType_REMOTE_FRAME : CanMessage_RtrType_DATA_FRAME,
      {msg.data.msg.len, {0}}
    };
    for (uint8_t i=0; i<msgRec.data.size; i++) {
      msgRec.data.bytes[i] = msg.data.msg.data[i];
    }

    rec.which_payload = DataloggerRecord_receivedCanMessage_tag;
    rec.payload.receivedCanMessage = msgRec;
  } else {
    CanError msgErr = {
      CanError_ErrorSource_UNKNOWN
    };

    switch(msg.data.errId) {
    case EwIRQ:
      msgErr.source = CanError_ErrorSource_ERROR_WARNING;
      break;
    case DoIRQ:
      msgErr.source = CanError_ErrorSource_DATA_OVERRUN;
      break;
    case EpIRQ:
      msgErr.source = CanError_ErrorSource_ERROR_PASSIVE;
      break;
    case AlIRQ:
      msgErr.source = CanError_ErrorSource_ARBITRATION_LOST;
      break;
    case BeIRQ:
      msgErr.source = CanError_ErrorSource_BUS_OFF;
      break;
    case WuIRQ:
      msgErr.source = CanError_ErrorSource_UNKNOWN;
      break;
    }

    rec.which_payload = DataloggerRecord_canError_tag;
    rec.payload.canError = msgErr;
  }

  return rec;
}


DataloggerRecord generateInfoRecord(const char* info, uint8_t sourceId, uint32_t timestampMs) {
  DataloggerRecord rec = {
    timestampMs,
    0,
    sourceId,
    DataloggerRecord_info_tag, {}
  };
  rec.payload.info = InfoString {
    ""
  };
  strcpy(rec.payload.info.info, info);
  // TODO: replace with strcpy_s once available
  //strcpy_s(rec.payload.value.info.info, sizeof(infoRec.info), info);

  return rec;
}
