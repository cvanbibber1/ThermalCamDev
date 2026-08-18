#include "protocol/wire_protocol.h"

#include "protocol/cobs.h"
#include "protocol/crc.h"

#include <string.h>

uint16_t wire_get_u16(const uint8_t *data) {
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

uint32_t wire_get_u32(const uint8_t *data) {
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

void wire_put_u16(uint8_t *data, uint16_t value) {
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8);
}

void wire_put_u32(uint8_t *data, uint32_t value) {
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8);
  data[2] = (uint8_t)(value >> 16);
  data[3] = (uint8_t)(value >> 24);
}

size_t wire_encode(const wire_message_t *message, uint8_t *output, size_t capacity) {
  uint8_t raw[WIRE_MAX_RAW_SIZE];
  if ((message == NULL) || (output == NULL) ||
      (message->payload_length > WIRE_MAX_PAYLOAD) || (capacity < 2U)) {
    return 0U;
  }

  wire_put_u16(&raw[0], WIRE_MAGIC);
  raw[2] = WIRE_VERSION;
  raw[3] = message->kind;
  raw[4] = message->flags;
  raw[5] = message->source;
  raw[6] = message->destination;
  raw[7] = 0U;
  wire_put_u32(&raw[8], message->sequence);
  wire_put_u16(&raw[12], message->opcode);
  wire_put_u16(&raw[14], message->payload_length);
  if (message->payload_length != 0U) {
    memcpy(&raw[WIRE_HEADER_SIZE], message->payload, message->payload_length);
  }

  size_t raw_length = WIRE_HEADER_SIZE + message->payload_length;
  wire_put_u32(&raw[raw_length], crc32c(raw, raw_length));
  raw_length += WIRE_CRC_SIZE;

  size_t encoded = cobs_encode(raw, raw_length, output, capacity - 1U);
  if (encoded == 0U) {
    return 0U;
  }
  output[encoded] = 0U;
  return encoded + 1U;
}

bool wire_decode(const uint8_t *encoded, size_t encoded_length, wire_message_t *message) {
  uint8_t raw[WIRE_MAX_RAW_SIZE];
  if ((encoded == NULL) || (message == NULL) || (encoded_length == 0U)) {
    return false;
  }
  if (encoded[encoded_length - 1U] == 0U) {
    --encoded_length;
  }
  size_t raw_length = cobs_decode(encoded, encoded_length, raw, sizeof(raw));
  if (raw_length < WIRE_HEADER_SIZE + WIRE_CRC_SIZE) {
    return false;
  }
  if ((wire_get_u16(&raw[0]) != WIRE_MAGIC) || (raw[2] != WIRE_VERSION)) {
    return false;
  }
  uint16_t payload_length = wire_get_u16(&raw[14]);
  if ((payload_length > WIRE_MAX_PAYLOAD) ||
      (raw_length != WIRE_HEADER_SIZE + payload_length + WIRE_CRC_SIZE)) {
    return false;
  }
  uint32_t expected = wire_get_u32(&raw[raw_length - WIRE_CRC_SIZE]);
  if (crc32c(raw, raw_length - WIRE_CRC_SIZE) != expected) {
    return false;
  }

  message->kind = raw[3];
  message->flags = raw[4];
  message->source = raw[5];
  message->destination = raw[6];
  message->sequence = wire_get_u32(&raw[8]);
  message->opcode = wire_get_u16(&raw[12]);
  message->payload_length = payload_length;
  if (payload_length != 0U) {
    memcpy(message->payload, &raw[WIRE_HEADER_SIZE], payload_length);
  }
  return true;
}

void wire_stream_init(wire_stream_t *stream) {
  if (stream != NULL) {
    memset(stream, 0, sizeof(*stream));
  }
}

bool wire_stream_push(wire_stream_t *stream, uint8_t byte, wire_message_t *message) {
  if ((stream == NULL) || (message == NULL)) {
    return false;
  }
  if (byte != 0U) {
    if (stream->length < sizeof(stream->encoded)) {
      stream->encoded[stream->length++] = byte;
    } else {
      stream->length = 0U;
      ++stream->overflows;
    }
    return false;
  }
  if (stream->length == 0U) {
    return false;
  }
  bool valid = wire_decode(stream->encoded, stream->length, message);
  stream->length = 0U;
  if (!valid) {
    ++stream->decode_errors;
  }
  return valid;
}

