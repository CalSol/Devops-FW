#include "slcan.h"

USBSLCANBase::USBSLCANBase(NonBlockingUSBSerial& stream)
    : stream(stream),
      messageQueued(false),
      outputPacketLen(0) {
    
}

/* Check if any bytes are available at the input */
bool USBSLCANBase::inputReadable() const {
    return stream.readable();
}

/* Read a single byte from the input */
int USBSLCANBase::readInputByte() {
    return stream.getc();
}

/* Parse and execute a single SLCAN command and enqueue the response */
bool USBSLCANBase::processCommands() {
    // Buffer an entire command
    bool active = readCommand();

    // Process the current command if there's space to send the response
    if (commandQueued) {
        size_t responseLength = commandResponseLength(inputCommandBuffer);
        if ((outputPacketLen + responseLength) <= sizeof(outputPacketBuffer)) {
            char* packetTail = &outputPacketBuffer[outputPacketLen];
            if (execCommand(inputCommandBuffer, packetTail)) {
                // Success
                packetTail[responseLength-1] = '\r';
                outputPacketLen += responseLength;
            } else {
                // Failure
                outputPacketBuffer[outputPacketLen++] = '\a';
            }
            commandQueued = false;
            active = true;
        }
    }
    
    return active;
}

/* Read and enqueue as many received CAN messages as will fit */
bool USBSLCANBase::processCANMessages() {
    bool active = false;
    
    size_t bytesAvailable = sizeof(outputPacketBuffer) - outputPacketLen;
    char* packetTail = &outputPacketBuffer[outputPacketLen];
    
    if (messageQueued) {
        size_t bytesConsumed = formatCANMessage(queuedMessage, packetTail, bytesAvailable);
        if (bytesConsumed > 0) {
            active = true;
            messageQueued = false;
            bytesAvailable -= bytesConsumed;
            packetTail += bytesConsumed;
            outputPacketLen += bytesConsumed;
        }
    }
    
    if (!messageQueued) {
        while (getNextCANMessage(queuedMessage)) {
            size_t bytesConsumed = formatCANMessage(queuedMessage, packetTail, bytesAvailable);
            if (bytesConsumed > 0) {
                active = true;
                bytesAvailable -= bytesConsumed;
                packetTail += bytesConsumed;
                outputPacketLen += bytesConsumed;
            } else {
                messageQueued = true;
                break;
            }
        }
    }

    return active;
}

/* Attempt to transmit the output queue */
bool USBSLCANBase::flush() {
    bool active = false;
    if (outputPacketLen > 0) {
        bool sent = stream.writeBlockNB((uint8_t*)(outputPacketBuffer),
                                        (uint16_t)(outputPacketLen));
        if (sent) {
            active = true;
            outputPacketLen = 0;
        }
    }
    return active;
}

/* Reset internal buffers because the host disconnected */
void USBSLCANBase::reset() {
    outputPacketLen = 0;
    messageQueued = false;
}

/* an SLCAN implementation that only accesses the CAN peripheral through callbacks */
USBSLCANSlave::USBSLCANSlave(NonBlockingUSBSerial& stream)
    : USBSLCANBase(stream),
      ignoreConfigCommands(false) {
}

/* Reset internal buffers because the host disconnected */
void USBSLCANSlave::reset() {
    USBSLCANBase::reset();
    messageBuffer.reset();
}

/* Configure SLCAN to silently discard mode/baudrate commands */
void USBSLCANSlave::setIgnoreConfigCommands(bool ignore) {
    ignoreConfigCommands = ignore;
}

/* Register the handler to change the CAN bitrate on request */
void USBSLCANSlave::setBaudrateHandler(Callback<bool(int baudrate)> callback) {
    cbSetBaudrate = callback;
}

/* Register the handler to change the CAN mode on request */
void USBSLCANSlave::setModeHandler(Callback<bool(CAN::Mode mode)> callback) {
    cbSetMode = callback;
}

/* Register the handler to send a CAN message on request */
void USBSLCANSlave::setTransmitHandler(Callback<bool(const CANMessage& msg)> callback) {
    cbTransmitMessage = callback;
}

bool USBSLCANSlave::setBaudrate(int baudrate) {
    if (ignoreConfigCommands) {
        return true;
    } else {
        return cbSetBaudrate.call(baudrate);
    }
}

bool USBSLCANSlave::setMode(CAN::Mode mode) {
    if (ignoreConfigCommands) {
        return true;
    } else {
        return cbSetMode.call(mode);
    }
}

bool USBSLCANSlave::transmitMessage(const CANMessage& msg) {
    return cbTransmitMessage.call(msg);
}

bool USBSLCANSlave::putCANMessage(const CANMessage& msg) {
    if (!messageBuffer.full()) {
        messageBuffer.push(msg);
        return true;
    }
    return false;
}

bool USBSLCANSlave::getNextCANMessage(CANMessage& msg) {
    return messageBuffer.pop(msg);
}
