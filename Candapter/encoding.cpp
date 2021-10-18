/*
 * encoding.cpp
 *
 *  Created on: Mar 8, 2015
 *      Author: Devan
 */

#include "encoding.h"
#include <mbed.h>

uint32_t cobs_encode(uint8_t* inBuffer, uint32_t inLen, uint8_t* outBuffer) {
  const uint8_t* inEnd = inBuffer + inLen;
  uint8_t* lastCodePos = outBuffer;
  uint8_t* outPos = outBuffer+1;
  while (inBuffer < inEnd) {
    if (*inBuffer == 0) {
      *lastCodePos = outPos - lastCodePos;
      lastCodePos = outPos;
      outPos++;
    } else {
      *outPos = *inBuffer;
      outPos++;
    }
    inBuffer++;
  }
  *lastCodePos = outPos - lastCodePos;
  return outPos - outBuffer;
}

uint32_t TachyonEncoding::encode(const mbed::CANMessage& msg, uint8_t* buffer) {
	uint8_t inBuffer[TachyonEncoding::MAX_ENCODED_SIZE];
	uint32_t length;

	inBuffer[0] = (uint8_t)(msg.id & 0xFF);
	inBuffer[1] = (uint8_t)((msg.id >> 8) & 0x0F);
	inBuffer[1] |= ((uint8_t)(msg.len) << 4);
	for(int i = 0; i < msg.len; i++) {
		inBuffer[2+i] = msg.data[i];
	}

	// Checksum
	inBuffer[msg.len+2] = 0;
	for(int i = 0 ; i < msg.len+2; i++)
	{
	  inBuffer[msg.len+2] -= inBuffer[i];
	}

	length = cobs_encode(inBuffer,msg.len+3,buffer);
	buffer[length] = TachyonEncoding::DELIMITER_BYTE;
	return length+1;
}
