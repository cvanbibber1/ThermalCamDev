#include "protocol/stp_protocol.h"

#include "protocol/crc.h"

#include <string.h>

/* CRC covers everything after the sync bytes up to but excluding the CRC
 * itself. The specification states this explicitly for HRT and the same
 * coverage is applied to the other packet classes for consistency. */
#define STP_CRC_START 4U

void stp_put_u16(uint8_t *out, uint16_t value) {
#if STP_BIG_ENDIAN
  out[0] = (uint8_t)(value >> 8);
  out[1] = (uint8_t)value;
#else
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
#endif
}

void stp_put_u32(uint8_t *out, uint32_t value) {
#if STP_BIG_ENDIAN
  out[0] = (uint8_t)(value >> 24);
  out[1] = (uint8_t)(value >> 16);
  out[2] = (uint8_t)(value >> 8);
  out[3] = (uint8_t)value;
#else
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
  out[2] = (uint8_t)(value >> 16);
  out[3] = (uint8_t)(value >> 24);
#endif
}

uint16_t stp_get_u16(const uint8_t *in) {
#if STP_BIG_ENDIAN
  return (uint16_t)(((uint16_t)in[0] << 8) | in[1]);
#else
  return (uint16_t)(((uint16_t)in[1] << 8) | in[0]);
#endif
}

uint32_t stp_get_u32(const uint8_t *in) {
#if STP_BIG_ENDIAN
  return ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) |
         ((uint32_t)in[2] << 8) | in[3];
#else
  return ((uint32_t)in[3] << 24) | ((uint32_t)in[2] << 16) |
         ((uint32_t)in[1] << 8) | in[0];
#endif
}

uint16_t stp_crc16(const uint8_t *data, size_t length) {
  return crc16_ccitt(data, length, STP_CRC16_SEED);
}

/* The sync value is written most significant byte first when STP_BIG_ENDIAN,
 * so the expected sequence is derived from the same switch as every other
 * field rather than being spelled out twice. */
static void sync_bytes(uint8_t out[4]) { stp_put_u32(out, STP_SYNC_WORD); }

void stp_receiver_init(stp_receiver_t *receiver) {
  if (receiver != NULL) {
    memset(receiver, 0, sizeof(*receiver));
  }
}

/* Drop the partially assembled packet and hunt for sync again, keeping the
 * diagnostic counters. They describe the link, not the packet in hand. */
static void restart(stp_receiver_t *receiver) {
  receiver->length = 0U;
  receiver->expected = 0U;
  receiver->sync_match = 0U;
}

/* Total packet length for a type arriving from DICE, or 0 if the type is not
 * one this experiment accepts. */
static uint16_t received_size_for_type(uint8_t type) {
  switch (type) {
    case STP_TYPE_COMMAND:
      return STP_COMMAND_SIZE;
    case STP_TYPE_LRT:
    case STP_TYPE_HRT_STOP:
    case STP_TYPE_HRT_STOP_WITH_LOSS:
    case STP_TYPE_HRT_GO:
      return STP_REQUEST_SIZE;
    default:
      return 0U;
  }
}

static stp_rx_kind_t kind_for_type(uint8_t type) {
  switch (type) {
    case STP_TYPE_COMMAND:
      return STP_RX_COMMAND;
    case STP_TYPE_LRT:
      return STP_RX_LRT_REQUEST;
    case STP_TYPE_HRT_STOP:
      return STP_RX_HRT_STOP;
    case STP_TYPE_HRT_STOP_WITH_LOSS:
      return STP_RX_HRT_STOP_WITH_LOSS;
    case STP_TYPE_HRT_GO:
      return STP_RX_HRT_GO;
    default:
      return STP_RX_NONE;
  }
}

bool stp_receiver_push(stp_receiver_t *receiver, uint8_t byte,
                       stp_rx_packet_t *packet) {
  if ((receiver == NULL) || (packet == NULL)) {
    return false;
  }

  uint8_t expected_sync[4];
  sync_bytes(expected_sync);

  /* Hunt for the sync pattern one byte at a time. Restarting the match on the
   * current byte rather than discarding it keeps a sync that begins inside a
   * partial match from being missed. */
  if (receiver->sync_match < 4U) {
    if (byte == expected_sync[receiver->sync_match]) {
      receiver->buffer[receiver->sync_match] = byte;
      ++receiver->sync_match;
    } else {
      receiver->sync_match = (byte == expected_sync[0]) ? 1U : 0U;
      if (receiver->sync_match == 1U) {
        receiver->buffer[0] = byte;
      }
    }
    receiver->length = receiver->sync_match;
    receiver->expected = 0U;
    return false;
  }

  if (receiver->length < sizeof(receiver->buffer)) {
    receiver->buffer[receiver->length] = byte;
  }
  ++receiver->length;

  /* Once the fixed preamble is in, the type at offset 10 gives the length. */
  if ((receiver->expected == 0U) && (receiver->length == STP_RX_HEADER_SIZE)) {
    receiver->expected = received_size_for_type(receiver->buffer[10]);
    if (receiver->expected == 0U) {
      ++receiver->type_errors;
      restart(receiver);
      return false;
    }
  }

  if ((receiver->expected == 0U) || (receiver->length < receiver->expected)) {
    return false;
  }

  uint16_t total = receiver->expected;
  bool valid = false;
  uint16_t stored = stp_get_u16(&receiver->buffer[total - 2U]);
  if (stored == stp_crc16(&receiver->buffer[STP_CRC_START],
                          (size_t)(total - 2U - STP_CRC_START))) {
    packet->kind = kind_for_type(receiver->buffer[10]);
    packet->coarse_time = stp_get_u32(&receiver->buffer[4]);
    packet->fine_time = stp_get_u16(&receiver->buffer[8]);
    packet->target_id = receiver->buffer[11];
    if (packet->kind == STP_RX_COMMAND) {
      packet->payload = &receiver->buffer[STP_COMMAND_PAYLOAD_OFFSET];
      packet->payload_length = STP_COMMAND_PAYLOAD_SIZE;
    } else {
      packet->payload = NULL;
      packet->payload_length = 0U;
    }
    ++receiver->accepted;
    valid = true;
  } else {
    ++receiver->crc_errors;
  }

  /* The buffer is reused for the next packet either way; a rejected packet
   * must not leave a partial match behind. */
  restart(receiver);
  return valid;
}

/* Common tail: stamp the CRC over everything after the sync bytes. */
static size_t finish(uint8_t *out, size_t total) {
  stp_put_u16(&out[total - 2U],
              stp_crc16(&out[STP_CRC_START], total - 2U - STP_CRC_START));
  return total;
}

size_t stp_build_ack(uint8_t *out, size_t capacity, uint8_t target_id) {
  if ((out == NULL) || (capacity < STP_ACK_SIZE)) {
    return 0U;
  }
  memset(out, 0, STP_ACK_SIZE);
  sync_bytes(out);
  out[4] = STP_TYPE_COMMAND;
  out[5] = target_id;
  return finish(out, STP_ACK_SIZE);
}

/* LRT and HRT data packets share a layout: sync, type, target, fixed payload,
 * CRC. Only the sizes and the type code differ. */
static size_t build_data(uint8_t *out, size_t capacity, uint8_t type,
                         uint8_t target_id, size_t total, size_t payload_size,
                         const uint8_t *payload, size_t payload_length) {
  if ((out == NULL) || (capacity < total)) {
    return 0U;
  }
  memset(out, 0, total);
  sync_bytes(out);
  out[4] = type;
  out[5] = target_id;
  if ((payload != NULL) && (payload_length > 0U)) {
    size_t copy = payload_length < payload_size ? payload_length : payload_size;
    memcpy(&out[6], payload, copy);
  }
  return finish(out, total);
}

size_t stp_build_lrt_data(uint8_t *out, size_t capacity, uint8_t target_id,
                          const uint8_t *payload, size_t payload_length) {
  return build_data(out, capacity, STP_TYPE_LRT_DATA, target_id, STP_LRT_DATA_SIZE,
                    STP_LRT_PAYLOAD_SIZE, payload, payload_length);
}

size_t stp_build_hrt_data(uint8_t *out, size_t capacity, uint8_t target_id,
                          const uint8_t *payload, size_t payload_length) {
  return build_data(out, capacity, STP_TYPE_HRT_DATA, target_id, STP_HRT_DATA_SIZE,
                    STP_HRT_PAYLOAD_SIZE, payload, payload_length);
}
