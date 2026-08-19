#include <unity.h>

#include "protocol/cobs.h"
#include "protocol/crc.h"
#include "protocol/stp_protocol.h"
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


/* ------------------------------------------------------------- STP RS-422 -- */

/* Build a DICE-to-experiment packet the way the flight computer would. */
static size_t make_request(uint8_t *out, uint8_t type, uint8_t target,
                           uint32_t coarse, uint16_t fine) {
  size_t total = (type == STP_TYPE_COMMAND) ? STP_COMMAND_SIZE : STP_REQUEST_SIZE;
  memset(out, 0, total);
  stp_put_u32(&out[0], STP_SYNC_WORD);
  stp_put_u32(&out[4], coarse);
  stp_put_u16(&out[8], fine);
  out[10] = type;
  out[11] = target;
  stp_put_u16(&out[total - 2U], stp_crc16(&out[4], total - 6U));
  return total;
}

static bool feed(stp_receiver_t *rx, const uint8_t *data, size_t length,
                 stp_rx_packet_t *packet) {
  bool got = false;
  for (size_t i = 0U; i < length; ++i) {
    if (stp_receiver_push(rx, data[i], packet)) {
      got = true;
    }
  }
  return got;
}

static void test_stp_accepts_each_request_type(void) {
  const uint8_t types[] = {STP_TYPE_COMMAND, STP_TYPE_LRT, STP_TYPE_HRT_STOP,
                           STP_TYPE_HRT_STOP_WITH_LOSS, STP_TYPE_HRT_GO};
  const stp_rx_kind_t kinds[] = {STP_RX_COMMAND, STP_RX_LRT_REQUEST,
                                 STP_RX_HRT_STOP, STP_RX_HRT_STOP_WITH_LOSS,
                                 STP_RX_HRT_GO};
  for (size_t i = 0U; i < sizeof(types); ++i) {
    stp_receiver_t rx;
    stp_receiver_init(&rx);
    uint8_t packet[STP_COMMAND_SIZE];
    size_t length = make_request(packet, types[i], 0x07U, 0x11223344U, 0x5566U);
    stp_rx_packet_t parsed;
    TEST_ASSERT_TRUE(feed(&rx, packet, length, &parsed));
    TEST_ASSERT_EQUAL_INT(kinds[i], parsed.kind);
    TEST_ASSERT_EQUAL_UINT8(0x07U, parsed.target_id);
    TEST_ASSERT_EQUAL_UINT32(0x11223344U, parsed.coarse_time);
    TEST_ASSERT_EQUAL_UINT16(0x5566U, parsed.fine_time);
  }
}

static void test_stp_command_exposes_payload(void) {
  stp_receiver_t rx;
  stp_receiver_init(&rx);
  uint8_t packet[STP_COMMAND_SIZE];
  (void)make_request(packet, STP_TYPE_COMMAND, 1U, 0U, 0U);
  for (size_t i = 0U; i < STP_COMMAND_PAYLOAD_SIZE; ++i) {
    packet[STP_COMMAND_PAYLOAD_OFFSET + i] = (uint8_t)(i + 1U);
  }
  stp_put_u16(&packet[STP_COMMAND_SIZE - 2U],
              stp_crc16(&packet[4], STP_COMMAND_SIZE - 6U));

  stp_rx_packet_t parsed;
  TEST_ASSERT_TRUE(feed(&rx, packet, STP_COMMAND_SIZE, &parsed));
  TEST_ASSERT_EQUAL_UINT16(STP_COMMAND_PAYLOAD_SIZE, parsed.payload_length);
  TEST_ASSERT_EQUAL_UINT8(1U, parsed.payload[0]);
  TEST_ASSERT_EQUAL_UINT8(STP_COMMAND_PAYLOAD_SIZE, parsed.payload[STP_COMMAND_PAYLOAD_SIZE - 1U]);
}

static void test_stp_rejects_corrupted_packet(void) {
  stp_receiver_t rx;
  stp_receiver_init(&rx);
  uint8_t packet[STP_COMMAND_SIZE];
  size_t length = make_request(packet, STP_TYPE_LRT, 1U, 0U, 0U);
  packet[12] ^= 0xFFU; /* inside the CRC coverage */
  stp_rx_packet_t parsed;
  TEST_ASSERT_FALSE(feed(&rx, packet, length, &parsed));
  TEST_ASSERT_EQUAL_UINT32(1U, rx.crc_errors);
  TEST_ASSERT_EQUAL_UINT32(0U, rx.accepted);
}

static void test_stp_resynchronizes_after_noise(void) {
  stp_receiver_t rx;
  stp_receiver_init(&rx);
  stp_rx_packet_t parsed;
  /* Leading rubbish, including a false start on the first sync byte, must not
   * stop the following packet from being found. */
  const uint8_t noise[] = {0x00U, 0x1AU, 0xFFU, 0x1AU, 0xCFU, 0x11U};
  TEST_ASSERT_FALSE(feed(&rx, noise, sizeof(noise), &parsed));
  uint8_t packet[STP_COMMAND_SIZE];
  size_t length = make_request(packet, STP_TYPE_HRT_GO, 3U, 9U, 4U);
  TEST_ASSERT_TRUE(feed(&rx, packet, length, &parsed));
  TEST_ASSERT_EQUAL_INT(STP_RX_HRT_GO, parsed.kind);
  TEST_ASSERT_EQUAL_UINT8(3U, parsed.target_id);
}

static void test_stp_ignores_unknown_packet_type(void) {
  stp_receiver_t rx;
  stp_receiver_init(&rx);
  uint8_t packet[STP_COMMAND_SIZE];
  size_t length = make_request(packet, 0x42U, 1U, 0U, 0U);
  stp_rx_packet_t parsed;
  TEST_ASSERT_FALSE(feed(&rx, packet, length, &parsed));
  TEST_ASSERT_EQUAL_UINT32(1U, rx.type_errors);
}

static void test_stp_builds_transmit_packets(void) {
  uint8_t out[STP_HRT_DATA_SIZE];

  TEST_ASSERT_EQUAL_UINT32(STP_ACK_SIZE, stp_build_ack(out, sizeof(out), 0x0AU));
  TEST_ASSERT_EQUAL_UINT32(STP_SYNC_WORD, stp_get_u32(&out[0]));
  TEST_ASSERT_EQUAL_UINT8(STP_TYPE_COMMAND, out[4]);
  TEST_ASSERT_EQUAL_UINT8(0x0AU, out[5]);
  TEST_ASSERT_EQUAL_UINT16(stp_crc16(&out[4], STP_ACK_SIZE - 6U),
                           stp_get_u16(&out[STP_ACK_SIZE - 2U]));

  uint8_t payload[8] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
  TEST_ASSERT_EQUAL_UINT32(STP_LRT_DATA_SIZE,
                           stp_build_lrt_data(out, sizeof(out), 2U, payload, sizeof(payload)));
  TEST_ASSERT_EQUAL_UINT8(STP_TYPE_LRT_DATA, out[4]);
  TEST_ASSERT_EQUAL_UINT8(1U, out[6]);
  /* Short payloads are zero padded to the fixed field. */
  TEST_ASSERT_EQUAL_UINT8(0U, out[6 + sizeof(payload)]);
  TEST_ASSERT_EQUAL_UINT16(stp_crc16(&out[4], STP_LRT_DATA_SIZE - 6U),
                           stp_get_u16(&out[STP_LRT_DATA_SIZE - 2U]));

  TEST_ASSERT_EQUAL_UINT32(STP_HRT_DATA_SIZE,
                           stp_build_hrt_data(out, sizeof(out), 2U, payload, sizeof(payload)));
  TEST_ASSERT_EQUAL_UINT8(STP_TYPE_HRT_DATA, out[4]);
  TEST_ASSERT_EQUAL_UINT16(stp_crc16(&out[4], STP_HRT_DATA_SIZE - 6U),
                           stp_get_u16(&out[STP_HRT_DATA_SIZE - 2U]));
}

static void test_stp_rejects_undersized_destination(void) {
  uint8_t small[4];
  TEST_ASSERT_EQUAL_UINT32(0U, stp_build_ack(small, sizeof(small), 1U));
  TEST_ASSERT_EQUAL_UINT32(0U, stp_build_lrt_data(small, sizeof(small), 1U, NULL, 0U));
  TEST_ASSERT_EQUAL_UINT32(0U, stp_build_hrt_data(small, sizeof(small), 1U, NULL, 0U));
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
  RUN_TEST(test_stp_accepts_each_request_type);
  RUN_TEST(test_stp_command_exposes_payload);
  RUN_TEST(test_stp_rejects_corrupted_packet);
  RUN_TEST(test_stp_resynchronizes_after_noise);
  RUN_TEST(test_stp_ignores_unknown_packet_type);
  RUN_TEST(test_stp_builds_transmit_packets);
  RUN_TEST(test_stp_rejects_undersized_destination);
  return UNITY_END();
}
