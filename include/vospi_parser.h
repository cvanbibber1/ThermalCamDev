#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_config.h"

#define VOSPI_PACKET_SIZE 164U
#define VOSPI_PACKETS_PER_SEGMENT 60U
#define VOSPI_SEGMENT_SIZE (VOSPI_PACKET_SIZE * VOSPI_PACKETS_PER_SEGMENT)

typedef enum {
  VOSPI_RESULT_IGNORED = 0,
  VOSPI_RESULT_SEGMENT = 1,
  VOSPI_RESULT_FRAME = 2,
  VOSPI_RESULT_DISCARD = -1,
  VOSPI_RESULT_CRC_ERROR = -2,
  VOSPI_RESULT_SEQUENCE_ERROR = -3,
} vospi_result_t;

typedef struct {
  uint16_t frames[2][APP_FRAME_PIXELS];
  uint8_t write_index;
  uint8_t published_index;
  uint8_t expected_segment;
  bool hold;
  uint32_t generation;
} vospi_parser_t;

void vospi_parser_init(vospi_parser_t *parser);
vospi_result_t vospi_parse_segment(vospi_parser_t *parser,
                                   const uint8_t segment[VOSPI_SEGMENT_SIZE],
                                   bool verify_crc);
const uint16_t *vospi_latest_frame(const vospi_parser_t *parser, uint32_t *generation);
/* While held, assembly continues into the working buffer but the published
 * frame and its generation stay fixed, so a chunked reader sees one image. */
void vospi_set_hold(vospi_parser_t *parser, bool hold);
uint16_t vospi_packet_crc(const uint8_t packet[VOSPI_PACKET_SIZE]);

