/*
 * can_struct.h
 *
 *  Created on: Jun 9, 2014
 *      Author: Devan
 *
 *  Common CAN message payload structs
 */

#ifndef __TACHYON_COMMON_CAN_STRUCT_H__
#define __TACHYON_COMMON_CAN_STRUCT_H__
#include <stdint.h>
#include <stddef.h>

#include <mbed.h>

/* Helper template function for constructing CAN messages
 *
 * @param id the message's 11-bit CAN ID
 * @param data struct/value to send
 *
 * @returns
 *   CANMessage with the bytes and DLC of `data`
 *
 * Example usage:
 *   struct ComplexPayload data;
 *   data.foo = 2.0;
 *   data.bar = 0x10;
 *   can.write(makeMessage(CAN_ID_COMPLEX, data));
 */
template <class DataStruct>
CANMessage makeMessage(int id, const DataStruct& data) {
    static_assert(sizeof(DataStruct) <= 8, "Message payload too big");
    return CANMessage(id, reinterpret_cast<const char*>(&data), sizeof(DataStruct));
}

/* Helper template function for unpacking CAN messages
 *
 * @param msg the CANMessage object to unpack
 *
 * @returns
 *   Value of the CANMessage
 * Example usage:
 *   CANMessage msg;
 *   if (can.read(msg)) {
 *     if (msg.id == CAN_ID_COMPLEX && msg.len == sizeof(ComplexPayload)) {
 *       struct ComplexPayload data = unpackMessage(msg);
 *       if (data.foo > 4.0) {
 *         int baz = data.bar | 0x20;
 *       }
 *     }
 *   }
 */
template <typename DataStruct>
DataStruct unpackMessage(CANMessage& msg) {
    static_assert(offsetof(CANMessage, data) % 4 == 0, "Cast dangerous with struct-alignment");
    const char* sideStepStrictAliasing = reinterpret_cast<char*>(msg.data);
    char* sideStepConst = const_cast<char*>(sideStepStrictAliasing);
    return *(reinterpret_cast<DataStruct*>(sideStepConst));
}

struct CoreStatus {
  uint16_t temperature;  // in 0.01 degC
  uint16_t vref_bandgap;  // in mV
};

#endif /* __TACHYON_COMMON_CAN_STRUCT_H__ */
