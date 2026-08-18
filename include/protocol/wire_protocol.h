#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WIRE_MAGIC 0x4354U
#define WIRE_VERSION 1U
#define WIRE_HEADER_SIZE 16U
#define WIRE_CRC_SIZE 4U
#define WIRE_MAX_PAYLOAD 2048U
#define WIRE_MAX_RAW_SIZE (WIRE_HEADER_SIZE + WIRE_MAX_PAYLOAD + WIRE_CRC_SIZE)
#define WIRE_MAX_ENCODED_SIZE (WIRE_MAX_RAW_SIZE + (WIRE_MAX_RAW_SIZE / 254U) + 2U)

enum {
  WIRE_KIND_REQUEST = 1U,
  WIRE_KIND_RESPONSE = 2U,
  WIRE_KIND_EVENT = 3U,
  WIRE_KIND_FRAME_CHUNK = 4U,
};

enum {
  WIRE_FLAG_ERROR = 0x01U,
  WIRE_FLAG_MORE = 0x02U,
  WIRE_FLAG_ACK_REQUIRED = 0x04U,
};

typedef struct {
  uint8_t kind;
  uint8_t flags;
  uint8_t source;
  uint8_t destination;
  uint32_t sequence;
  uint16_t opcode;
  uint16_t payload_length;
  uint8_t payload[WIRE_MAX_PAYLOAD];
} wire_message_t;

typedef struct {
  uint8_t encoded[WIRE_MAX_ENCODED_SIZE];
  size_t length;
  uint32_t overflows;
  uint32_t decode_errors;
} wire_stream_t;

size_t wire_encode(const wire_message_t *message, uint8_t *output, size_t capacity);
bool wire_decode(const uint8_t *encoded, size_t encoded_length, wire_message_t *message);
void wire_stream_init(wire_stream_t *stream);
bool wire_stream_push(wire_stream_t *stream, uint8_t byte, wire_message_t *message);

uint16_t wire_get_u16(const uint8_t *data);
uint32_t wire_get_u32(const uint8_t *data);
void wire_put_u16(uint8_t *data, uint16_t value);
void wire_put_u32(uint8_t *data, uint32_t value);

