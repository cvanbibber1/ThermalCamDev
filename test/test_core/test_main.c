#include <unity.h>

#include "protocol/cobs.h"
#include "protocol/crc.h"
#include "protocol/wire_protocol.h"
#include "vospi_parser.h"

#include <string.h>

static void test_crc_known_vectors(void) {
  const uint8_t text[] = "123456789";
  TEST_ASSERT_EQUAL_HEX32(0xE3069283U, crc32c(text, 9U));
  TEST_ASSERT_EQUAL_HEX16(0x31C3U, crc16_ccitt(text, 9U, 0U));
}

static void test_cobs_round_trip(void) {
  const uint8_t input[] = {0U, 1U, 2U, 0U, 3U, 4U, 5U, 0U};
  uint8_t encoded[32];
  uint8_t decoded[32];
  size_t encoded_length = cobs_encode(input, sizeof(input), encoded, sizeof(encoded));
  TEST_ASSERT_GREATER_THAN(0U, encoded_length);
  size_t decoded_length = cobs_decode(encoded, encoded_length, decoded, sizeof(decoded));
  TEST_ASSERT_EQUAL_UINT(sizeof(input), decoded_length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(input, decoded, sizeof(input));
}

static void test_wire_round_trip_and_crc_rejection(void) {
  wire_message_t input = {0};
  input.kind = WIRE_KIND_REQUEST;
  input.flags = WIRE_FLAG_ACK_REQUIRED;
  input.source = 9U;
  input.destination = 2U;
  input.sequence = 0x12345678U;
  input.opcode = 0x0201U;
  input.payload_length = 5U;
  memcpy(input.payload, "A\0BCD", 5U);

  uint8_t encoded[WIRE_MAX_ENCODED_SIZE];
  size_t length = wire_encode(&input, encoded, sizeof(encoded));
  TEST_ASSERT_GREATER_THAN(1U, length);

  wire_message_t output;
  TEST_ASSERT_TRUE(wire_decode(encoded, length, &output));
  TEST_ASSERT_EQUAL_UINT8(input.kind, output.kind);
  TEST_ASSERT_EQUAL_UINT32(input.sequence, output.sequence);
  TEST_ASSERT_EQUAL_UINT16(input.opcode, output.opcode);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(input.payload, output.payload, input.payload_length);

  encoded[2] ^= 0x20U;
  TEST_ASSERT_FALSE(wire_decode(encoded, length, &output));
}

static void test_wire_stream_delimiter(void) {
  wire_message_t input = {0};
  input.kind = WIRE_KIND_REQUEST;
  input.sequence = 7U;
  input.opcode = 1U;
  uint8_t encoded[WIRE_MAX_ENCODED_SIZE];
  size_t length = wire_encode(&input, encoded, sizeof(encoded));
  wire_stream_t stream;
  wire_message_t output;
  wire_stream_init(&stream);
  bool complete = false;
  for (size_t i = 0U; i < length; ++i) {
    complete = wire_stream_push(&stream, encoded[i], &output);
  }
  TEST_ASSERT_TRUE(complete);
  TEST_ASSERT_EQUAL_UINT32(7U, output.sequence);
}

static void make_segment(uint8_t segment_id, uint8_t *segment, bool valid_crc) {
  memset(segment, 0, VOSPI_SEGMENT_SIZE);
  for (uint16_t packet_index = 0U; packet_index < VOSPI_PACKETS_PER_SEGMENT; ++packet_index) {
    uint8_t *packet = &segment[packet_index * VOSPI_PACKET_SIZE];
    packet[0] = packet_index == 20U ? (uint8_t)(segment_id << 4) : 0U;
    packet[1] = (uint8_t)packet_index;
    for (uint16_t pixel = 0U; pixel < 80U; ++pixel) {
      uint16_t value = (uint16_t)(segment_id * 1000U + packet_index * 80U + pixel);
      packet[4U + pixel * 2U] = (uint8_t)(value >> 8);
      packet[5U + pixel * 2U] = (uint8_t)value;
    }
    uint16_t crc = vospi_packet_crc(packet);
    packet[2] = (uint8_t)(crc >> 8);
    packet[3] = (uint8_t)crc;
  }
  if (!valid_crc) {
    segment[10] ^= 1U;
  }
}

static void test_vospi_assembles_four_segments(void) {
  static vospi_parser_t parser;
  static uint8_t segment[VOSPI_SEGMENT_SIZE];
  vospi_parser_init(&parser);
  for (uint8_t id = 1U; id <= 4U; ++id) {
    make_segment(id, segment, true);
    vospi_result_t result = vospi_parse_segment(&parser, segment, true);
    TEST_ASSERT_EQUAL_INT(id == 4U ? VOSPI_RESULT_FRAME : VOSPI_RESULT_SEGMENT, result);
  }
  uint32_t generation = 0U;
  const uint16_t *frame = vospi_latest_frame(&parser, &generation);
  TEST_ASSERT_NOT_NULL(frame);
  TEST_ASSERT_EQUAL_UINT32(1U, generation);
  TEST_ASSERT_EQUAL_UINT16(1000U, frame[0]);
  TEST_ASSERT_EQUAL_UINT16(2599U, frame[1599]);
  TEST_ASSERT_EQUAL_UINT16(4000U, frame[14400]);
}

static void test_vospi_rejects_bad_crc(void) {
  static vospi_parser_t parser;
  static uint8_t segment[VOSPI_SEGMENT_SIZE];
  vospi_parser_init(&parser);
  make_segment(1U, segment, false);
  TEST_ASSERT_EQUAL_INT(VOSPI_RESULT_CRC_ERROR,
                        vospi_parse_segment(&parser, segment, true));
}

static void test_vospi_recognizes_discard_before_segment_id(void) {
  vospi_parser_t parser;
  uint8_t segment[VOSPI_SEGMENT_SIZE] = {0};
  vospi_parser_init(&parser);

  /* Upper nibble 4 would look like an out-of-sequence segment if the discard
   * marker in the lower nibble were not checked first. */
  segment[20U * VOSPI_PACKET_SIZE] = 0x4FU;
  segment[20U * VOSPI_PACKET_SIZE + 1U] = 0xFFU;
  TEST_ASSERT_EQUAL_INT(VOSPI_RESULT_DISCARD,
                        vospi_parse_segment(&parser, segment, true));
  TEST_ASSERT_EQUAL_UINT8(1U, parser.expected_segment);
}

static void test_vospi_invalid_segment_resets_sequence(void) {
  static vospi_parser_t parser;
  static uint8_t segment[VOSPI_SEGMENT_SIZE];
  vospi_parser_init(&parser);
  make_segment(1U, segment, true);
  TEST_ASSERT_EQUAL_INT(VOSPI_RESULT_SEGMENT,
                        vospi_parse_segment(&parser, segment, true));
  TEST_ASSERT_EQUAL_UINT8(2U, parser.expected_segment);

  make_segment(0U, segment, true);
  TEST_ASSERT_EQUAL_INT(VOSPI_RESULT_IGNORED,
                        vospi_parse_segment(&parser, segment, true));
  TEST_ASSERT_EQUAL_UINT8(1U, parser.expected_segment);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_crc_known_vectors);
  RUN_TEST(test_cobs_round_trip);
  RUN_TEST(test_wire_round_trip_and_crc_rejection);
  RUN_TEST(test_wire_stream_delimiter);
  RUN_TEST(test_vospi_assembles_four_segments);
  RUN_TEST(test_vospi_rejects_bad_crc);
  RUN_TEST(test_vospi_recognizes_discard_before_segment_id);
  RUN_TEST(test_vospi_invalid_segment_resets_sequence);
  return UNITY_END();
}
