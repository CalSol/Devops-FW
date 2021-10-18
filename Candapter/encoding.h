/*
 * encoding.h
 *
 *  Created on: Mar 8, 2015
 *      Author: Devan
 */

#ifndef ENCODING_H_
#define ENCODING_H_

#include <stdint.h>
namespace mbed {
	class CANMessage;
}

namespace TachyonEncoding {
	// 2 bytes from ID, 1 for length, 8 for payload, 1 for delimiter and 1 extra overhead
	const uint32_t MAX_ENCODED_SIZE = 13;
	const uint8_t DELIMITER_BYTE = 0;
	uint32_t encode(const mbed::CANMessage& msg, uint8_t* buffer);
}


#endif /* ENCODING_H_ */
