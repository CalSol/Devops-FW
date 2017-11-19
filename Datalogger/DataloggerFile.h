#ifndef _DATALOGGER_FILE_H_
#define _DATALOGGER_FILE_H

#include "mbed.h"
#include "FileSystemLike.h"

#include "datalogger.pb.h"

class DataloggerFile {
public:
  DataloggerFile(FileSystemLike& filesystem) :
      filesystem_(filesystem), file_(NULL) {
  }

  bool newFile(const char* dirname, const char* basename);
  bool syncFile();
  bool closeFile();

protected:
  FileSystemLike& filesystem_;
  FileHandle* file_;  // currently open file, or NULL if none open
};

/**
 * Variant of DataloggerFile with COBS protobuf recording utilities.
 */
class DataloggerProtoFile : public DataloggerFile {
public:
  DataloggerProtoFile(FileSystemLike& filesystem) :
      DataloggerFile(filesystem) {
  }

  /**
   * Encodes a DataloggerRecord to wire format, COBS it, and writes it to the
   * open file. Returns true on success.
   *
   * Does nothing if no file is open.
   */
  bool write(const DataloggerRecord record);

protected:
  uint8_t encodingBuffer_[DataloggerRecord_size + (DataloggerRecord_size + 253) / 254 + 2];  // staticly allocate the buffer
};

#endif

