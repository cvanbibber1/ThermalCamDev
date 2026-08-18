#include "vospi_parser.h"

#include "protocol/crc.h"

#include <string.h>

void vospi_parser_init(vospi_parser_t *parser) {
  if (parser != NULL) {
    memset(parser, 0, sizeof(*parser));
    parser->expected_segment = 1U;
  }
}

uint16_t vospi_packet_crc(const uint8_t packet[VOSPI_PACKET_SIZE]) {
  /* The ID nibble and the CRC field itself are excluded from the checksum, so
   * only the first four bytes need substituting; the payload runs in one pass
   * instead of a call per byte. */
  const uint8_t header[4] = {(uint8_t)(packet[0] & 0x0FU), packet[1], 0U, 0U};
  uint16_t crc = crc16_ccitt(header, sizeof(header), 0U);
  return crc16_ccitt(&packet[4], VOSPI_PACKET_SIZE - 4U, crc);
}

static bool packet_is_discard(const uint8_t *packet) {
  return (packet[0] & 0x0FU) == 0x0FU;
}

vospi_result_t vospi_parse_segment(vospi_parser_t *parser,
                                   const uint8_t segment[VOSPI_SEGMENT_SIZE],
                                   bool verify_crc) {
  if ((parser == NULL) || (segment == NULL)) {
    return VOSPI_RESULT_SEQUENCE_ERROR;
  }
  const uint8_t *packet20 = &segment[20U * VOSPI_PACKET_SIZE];
  /* During VoSPI synchronization Lepton emits discard packets. Their upper
   * nibble is unspecified, so recognize the xFxx marker before interpreting
   * packet 20's upper nibble as a segment ID. */
  if (packet_is_discard(packet20)) {
    parser->expected_segment = 1U;
    return VOSPI_RESULT_DISCARD;
  }
  uint8_t segment_id = (packet20[0] >> 4) & 0x07U;
  if (segment_id == 0U) {
    parser->expected_segment = 1U;
    return VOSPI_RESULT_IGNORED;
  }
  if (segment_id > 4U) {
    parser->expected_segment = 1U;
    return VOSPI_RESULT_SEQUENCE_ERROR;
  }
  if (segment_id != parser->expected_segment) {
    if (parser->expected_segment == 1U) {
      return VOSPI_RESULT_IGNORED;
    }
    parser->expected_segment = 1U;
    return VOSPI_RESULT_SEQUENCE_ERROR;
  }

  for (uint16_t packet_index = 0U; packet_index < VOSPI_PACKETS_PER_SEGMENT; ++packet_index) {
    const uint8_t *packet = &segment[packet_index * VOSPI_PACKET_SIZE];
    if (packet_is_discard(packet)) {
      parser->expected_segment = 1U;
      return VOSPI_RESULT_DISCARD;
    }
    uint16_t packet_number = ((uint16_t)(packet[0] & 0x0FU) << 8) | packet[1];
    if (packet_number != packet_index) {
      parser->expected_segment = 1U;
      return VOSPI_RESULT_SEQUENCE_ERROR;
    }
    uint16_t expected_crc = ((uint16_t)packet[2] << 8) | packet[3];
    if (verify_crc && (vospi_packet_crc(packet) != expected_crc)) {
      parser->expected_segment = 1U;
      return VOSPI_RESULT_CRC_ERROR;
    }

    size_t pixel_base = ((size_t)(segment_id - 1U) * 30U * APP_FRAME_WIDTH) +
                        ((size_t)packet_index * 80U);
    for (size_t pixel = 0U; pixel < 80U; ++pixel) {
      size_t payload = 4U + pixel * 2U;
      parser->frames[parser->write_index][pixel_base + pixel] =
          ((uint16_t)packet[payload] << 8) | packet[payload + 1U];
    }
  }

  if (segment_id != 4U) {
    parser->expected_segment = segment_id + 1U;
    return VOSPI_RESULT_SEGMENT;
  }

  parser->expected_segment = 1U;
  if (!parser->hold) {
    parser->published_index = parser->write_index;
    parser->write_index ^= 1U;
    ++parser->generation;
  }
  return VOSPI_RESULT_FRAME;
}

void vospi_set_hold(vospi_parser_t *parser, bool hold) {
  if (parser != NULL) {
    parser->hold = hold;
  }
}

const uint16_t *vospi_latest_frame(const vospi_parser_t *parser, uint32_t *generation) {
  if (parser == NULL) {
    return NULL;
  }
  if (generation != NULL) {
    *generation = parser->generation;
  }
  return parser->generation == 0U ? NULL : parser->frames[parser->published_index];
}
