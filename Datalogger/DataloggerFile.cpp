#include "DataloggerFile.h"

#include <limits>
#include <utility>

#include "pb_common.h"
#include "pb_encode.h"

#define DEBUG_ENABLED
#include "debug.h"

static bool strPrefixMatch(const char* str1, const char* str2, size_t length) {
  while (length > 0) {
    if (*str1 != *str2) {
      return false;
    }
    str1++;
    str2++;
    length--;
  }
  return true;
}

static void reverse(char* str, char* end) {
  end--;
  while (str < end) {
    std::swap(*str, *end);
    str++;
    end--;
  }
}

static bool atoiLimited(const char* dst, size_t len, uint32_t* valOut) {
  *valOut = 0;
  const char* end = dst + len;
  while (dst < end) {
    *valOut *= 10;
    if (*dst >= '0' && *dst <= '9') {
      *valOut += *dst - '0';
    } else {
      return false;
    }
    dst++;
  }
  return true;
}

// Length-limited variable-size itoa. Returns bytes written, or 0 on failure.
static size_t itoaLimited(char* dst, uint32_t val, size_t len) {
  char* start = dst;
  char* end = dst + len;

  if (len == 0) {
    return 0;
  }
  if (val == 0) {  // special case, otherwise detects as 0-length
    *dst = '0';
    return 1;
  }
  while (val > 0) {
    if (dst >= end) {  // number too big to fit
      return 0;
    }
    uint8_t digit = val % 10;
    val /= 10;
    *dst = '0' + digit;
    dst++;
  }
  reverse(start, dst);
  return dst - start;
}

bool DataloggerFile::newFile(const char* dirname, const char* basename) {
  if (file_ != NULL) {
    debugWarn("File not null\r\n");
    file_ = NULL;
  }

  char filename[21];  // 8/8.3 format, disallow LFN (Long Filename)
  size_t dirnameLen = strlen(dirname);
  size_t basenameLen = strlen(basename);
  if (dirnameLen > 8 || basenameLen > 8) {
    // Don't overflow filename buffer
    return false;
  }
  strcpy(filename, dirname);
  filename[dirnameLen] = '/';
  strcpy(filename + dirnameLen + 1, basename);
  size_t basenameEnd = dirnameLen + 1 + basenameLen;

  DirHandle* dir;
  if (!filesystem_.open(&dir, dirname)) {
    debugInfo("Opened dir '%s'", dirname);
    struct dirent dirp;
    uint32_t nextFilenameSeq = 0;
    while (dir->read(&dirp) > 0) {
      const char* dirFilename = dirp.d_name;
      debugInfo("Found file '%s'", dirFilename);

      if (strPrefixMatch(basename, dirFilename, basenameLen)) {
        if (dirFilename[basenameLen] == '\0') {
          if (nextFilenameSeq == 0) {
            nextFilenameSeq = 1;
          }
        } else if (dirFilename[basenameLen] == '_') {
          // Determine length of postfix (sequence), between underscore and dot (or end-of-string)
          uint32_t seqLen = 0;
          while (dirFilename[basenameLen + 1 + seqLen] != '\0') {
            seqLen++;
          }
          uint32_t thisNameSeq;
          if (atoiLimited(dirFilename + basenameLen + 1, seqLen, &thisNameSeq)) {
            if (thisNameSeq >= nextFilenameSeq) {
              nextFilenameSeq = thisNameSeq + 1;
            }
          }
        }
      }
    }
    dir->close();

    if (nextFilenameSeq > 0) {
      if (basenameLen > 6) {
        debugWarn("basename '%s' too long for any sequence", basename);
        return false;  // no room for underscore separator and sequence id
      }

      size_t maxSeqCharacters = 8 - basenameLen - 1;
      filename[basenameEnd] = '_';
      size_t seqCharacters = itoaLimited(filename + basenameEnd + 1, nextFilenameSeq, maxSeqCharacters);
      if (seqCharacters == 0) {
        debugWarn("basename '%s' too long for sequence %i", basename, nextFilenameSeq);
        return false;
      }
      filename[basenameEnd + 1 + seqCharacters] = '\0';
    }
  } else {
    debugInfo("Creating dir '%s'", dirname);
    int retVal = filesystem_.mkdir(dirname, 0777);
    if (retVal) {
      debugWarn("Dir creation failed: %i", retVal);
      return false;
    }
    // Empty directory, don't need to mangle filename
  }

  debugInfo("Opening file '%s'", filename);
  if (!filesystem_.open(&file_, filename, O_WRONLY | O_CREAT | O_TRUNC)) {
    debugInfo("File open OK");
  } else {
    debugWarn("File open failed");
  }

  return file_ != NULL;
}

bool DataloggerFile::syncFile() {
  if (file_ == NULL) {
    return false;  // TODO: perhaps assert out?
  }
  int result = file_->sync();
  if (!result) {
    debugInfo("File sync");
  } else {
    debugWarn("File sync failed: %i", result);
  }
  return result == 0;
}

bool DataloggerFile::closeFile() {
  if (file_ == NULL) {
    return false;  // TODO: perhaps assert out?
  }
  int result = file_->close();
  if (!result) {
    debugInfo("File close");
  } else {
    debugWarn("File close failed: %i", result);
  }
  file_ = NULL;
  return result == 0;
}

struct pb_ostream_cobs_state {
  uint8_t* bufPos;  // pointer to next byte to be written to buffer
  uint8_t* bufEnd;  // pointer to one byte past end of buffer
  uint8_t* lastCodePos;  // pointer to last code byte, to be filled in at next zero
};

static bool pb_ostream_cobs_callback(pb_ostream_t *stream, const uint8_t *buf, size_t count) {
  pb_ostream_cobs_state* state = (pb_ostream_cobs_state*)stream->state;

  const uint8_t* bufEnd = buf + count;
  while (buf < bufEnd) {
    if (state->bufPos >= state->bufEnd) {
      return false;
    }
    if (*buf == 0) {
      *state->lastCodePos = state->bufPos - state->lastCodePos;
      state->lastCodePos = state->bufPos;
      state->bufPos++;
    } else {
      if ((state->bufPos - state->lastCodePos) >= 255) {
        *state->lastCodePos = 255;
        state->lastCodePos = state->bufPos;
        state->bufPos++;
        if (state->bufPos >= state->bufEnd) {
          return false;
        }
      }
      *state->bufPos = *buf;
      state->bufPos++;
    }
    buf++;
  }
  return true;
}

static bool pb_ostream_cobs_finish(pb_ostream_cobs_state* state) {
  *state->lastCodePos = state->bufPos - state->lastCodePos;
  return true;
}

static pb_ostream_t pb_ostream_cobs_from_buffer(uint8_t* buffer, size_t bufsize, pb_ostream_cobs_state* state) {
  state->lastCodePos = buffer;
  state->bufPos = buffer + 1;  // first byte is a code byte
  state->bufEnd = buffer + bufsize;
  return {&pb_ostream_cobs_callback, state, bufsize, 0};
}

bool DataloggerProtoFile::write(const DataloggerRecord record) {
  pb_ostream_cobs_state state;
  pb_ostream_t stream = pb_ostream_cobs_from_buffer(encodingBuffer_ + 1, sizeof(encodingBuffer_) - 1, &state);

  if (file_ != NULL
      && pb_encode(&stream, DataloggerRecord_fields, &record)
      && pb_ostream_cobs_finish(&state)) {
    encodingBuffer_[0] = 0;  // state of frame delimiter
    size_t bufferSize = state.bufPos - encodingBuffer_;
    return file_->write(encodingBuffer_, bufferSize) == bufferSize;
  } else {
    return false;
  }
}
