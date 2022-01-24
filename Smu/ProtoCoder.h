#include <stddef.h>

#include <pb.h>
#include <pb_encode.h>
#include <pb_decode.h>

#ifndef __PROTO_CODER_H__
#define __PROTO_CODER_H__

// Basic wrapper around protobuf that provides a simple encode_to and decode_from interface.
template<typename ProtoStruct>
class ProtoCoder {
public:
  ProtoCoder(const pb_msgdesc_t &fields, bool isDelimited = false, bool isNullTerminated = false) : 
      fields_(fields), isDelimited_(isDelimited), isNullTerminated_(isNullTerminated) {
    // TODO is there a better init other than decoding from a null?
    uint8_t nullBuffer[1] = {0};  // zero length
    pb_istream_t stream = pb_istream_from_buffer(nullBuffer, 1);
    pb_decode_ex(&stream, &fields_, &pb, PB_DECODE_DELIMITED);
  }

  ProtoStruct pb;  // should be externally modified

  // Serializes this proto to the target byte array. Returns success (true) or failure (false)
  bool encode_to(uint8_t *bytes, size_t bytesSize, unsigned int flags = 0) {
    flags |= encodingFlags();
    pb_ostream_t stream = pb_ostream_from_buffer(bytes, sizeof(bytesSize));
    return pb_encode_ex(&stream, &fields_, &pb, flags);
  }

  // Deserializes a proto from the target byte array, starting from a cleared out proto 
  // (discarding current values).
  bool decode_from(uint8_t *bytes, size_t bytesSize, unsigned int flags = 0) {
    flags |= decodingFlags();
    pb_istream_t stream = pb_istream_from_buffer(bytes, bytesSize);
    return pb_decode_ex(&stream, &fields_, &pb, flags);
  }

  // Deserialize a proto from the target byte array and merges over the current values.
  bool update_from(uint8_t *bytes, size_t bytesSize) {
    return decode_from(bytes, bytesSize, PB_DECODE_NOINIT);
  }

protected:
  const pb_msgdesc_t &fields_;
  const bool isDelimited_, isNullTerminated_;

  unsigned int encodingFlags() {
    unsigned int flags = 0;
    if (isDelimited_) {
      flags |= PB_ENCODE_DELIMITED;
    }
    if (isNullTerminated_) {
      flags |= PB_ENCODE_NULLTERMINATED;
    }
    return flags;
  }

  unsigned int decodingFlags() {
    unsigned int flags = 0;
    if (isDelimited_) {
      flags |= PB_DECODE_DELIMITED;
    }
    if (isNullTerminated_) {
      flags |= PB_DECODE_NULLTERMINATED;
    }
    return flags;
  }
};

// Wrapper around protobuf that includes a serialization buffer and allows an in-place update.
// BufferSize must account for size prefix and null terminator (if applicable).
template<typename ProtoStruct, size_t BufferSize>
class BufferedProtoCoder : public ProtoCoder<ProtoStruct> {
public:
  BufferedProtoCoder(const pb_msgdesc_t &fields, bool isDelimited = false, bool isNullTerminated = false) : ProtoCoder<ProtoStruct>(fields, isDelimited, isNullTerminated) {
  }

  // Serializes this proto to the internal buffer, returning the internal buffer (if successful) or NULL (if not).
  uint8_t* encode() {
    if (this->encode_to(bytes_, BufferSize)) {
      return bytes_;
    } else {
      return NULL;
    }
  }

  // Deserializes a proto from the internal buffer
  bool decode() {
    return this->decode_from(bytes_, BufferSize);
  }

  // Updates the current values from another proto, by serializing and deserializing the update.
  // Overwrites the internal buffer.
  // TODO is there a programmatic option?
  bool update_from(ProtoStruct other) {
    pb_ostream_t outStream = pb_ostream_from_buffer(bytes_, sizeof(bytes_));
    if (!pb_encode_ex(&outStream, &this->fields_, &other, PB_ENCODE_NULLTERMINATED)) {
      return false;
    }

    pb_istream_t inStream = pb_istream_from_buffer(bytes_, sizeof(bytes_));
    return pb_decode_ex(&inStream, &this->fields_, &this->pb, PB_DECODE_NOINIT | PB_DECODE_NULLTERMINATED);
  }
  
protected:
  uint8_t bytes_[BufferSize];
};

#endif
