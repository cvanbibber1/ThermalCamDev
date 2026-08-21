#include "stp_link.h"

#include "app_config.h"
#include "board.h"
#include "dosimeter.h"
#include "health.h"
#include "lepton_capture.h"
#include "protocol/frame_codec.h"
#include "protocol/stp_protocol.h"
#include "settings.h"

#include <string.h>

/* Bench bring-up: send vitals periodically and begin the image stream without
 * waiting for DICE, so the link can be proven with nothing but an RS-422 to
 * USB converter on the other end.
 *
 * This only changes the starting state and adds the periodic vitals. Flow
 * control is always obeyed: an HRT stop stops the stream in bench mode exactly
 * as it does in flight.
 *
 * MUST be 0 for flight. Free running means this node drives the bus without
 * being asked, which would collide with other experiments and with DICE. */
#ifndef STP_BENCH_FREERUN
#define STP_BENCH_FREERUN 1
#endif

/* Vitals cadence. LRT carries telemetry only, so it is small and infrequent
 * and is always given the transmitter first; at one packet a second it costs
 * about 1.4% of the link. */
#define STP_FREERUN_LRT_PERIOD_MS 1000U

/* HRT carries the image and is sent back to back whenever the transmitter is
 * idle, because the image rate is what the link limits.
 *
 * One HRT packet is 1288 bytes, about 14.0 ms at 921600 baud 8N1, so the link
 * carries at most about 71 packets a second. A frame is 38400 bytes, which is
 * 31 packets, so the ceiling is roughly 2.3 frames a second against the 8.8
 * the camera produces. Sending 16-bit radiometric pixels is what costs this;
 * an 8-bit image would roughly double the rate. */
#define STP_HRT_MIN_GAP_MS 0U

#define STP_RX_DMA_SIZE 512U

static uint8_t rx_dma[STP_RX_DMA_SIZE];
static uint16_t rx_position;
static uint8_t tx_buffer[STP_HRT_DATA_SIZE];
static stp_receiver_t receiver;
static stp_link_status_t current;

/* Image transmission state: a frame is sent as a run of numbered chunks so the
 * far end can reassemble it and discard partial frames. */
static uint32_t hrt_generation;
static uint16_t hrt_chunk;
/* Fixed for uncompressed frames, but a compressed one is a different size
 * every time, so both are latched when the frame starts. */
static uint16_t hrt_total_chunks;
static size_t hrt_length;
static uint32_t last_lrt_ms;
static uint32_t last_hrt_ms;
/* Replies owed to DICE. A request can arrive while an image packet is going
 * out, and the transmitter cannot be interrupted; without these the reply
 * would simply be dropped and the request would look ignored. */
static bool pending_ack;
static bool pending_lrt;
/* Capture sequencing. A capture always corrects the image first, so the frame
 * that goes down is not the drifted one that prompted the request. */
static uint32_t correcting_until_ms;
static bool burst_active;
static uint8_t pending_capture_target;

/* Encoded frames waiting to go out, and the frame they were coded against.
 *
 * There are two output slots so the encoder can start the next frame while the
 * current one is still draining down the link. With one slot the two ran end
 * to end -- about 50 ms of encoding then 126 ms of transmission -- and the
 * link sat idle for the first part of every cycle.
 *
 * The reference is private rather than borrowed from the parser's spare
 * buffer, because the parser reclaims that as soon as the next frame starts,
 * and the decoder needs the reference to match exactly what was sent. */
#if APP_CODEC_ENABLED
typedef struct {
  uint8_t buffer[APP_CODEC_BUFFER_BYTES];
  size_t length;   /* encoded size, once complete */
  uint32_t generation;
  uint8_t mode;
  bool complete;   /* the encoder has finished this frame */
  /* Set when the frame did not compress and is being sent straight from the
   * capture buffer instead of from `buffer`. */
  const uint8_t *raw_source;
} codec_slot_t;

static codec_slot_t codec_slots[2];
/* A two-slot ring. `write` is the slot the encoder takes next, `read` the one
 * the transmitter is working through, and `used` how many hold a frame that
 * has not finished going out. */
static uint8_t codec_write;
static uint8_t codec_read;
static uint8_t codec_used;
/* Index of the slot being encoded, or -1 when the encoder is idle. */
static int8_t codec_encoding = -1;

static uint16_t codec_reference[APP_FRAME_PIXELS];
static bool codec_reference_valid;
static uint32_t codec_frames_since_key;
static frame_codec_encoder_t codec_encoder;
static const uint16_t *codec_source_frame;
static uint32_t codec_last_generation;
static bool codec_keyframe;
/* Reported in vitals so the achieved compression is visible from the ground. */
static size_t codec_length;
#endif

static uint16_t chunks_of(size_t bytes) {
  return (uint16_t)((bytes + STP_HRT_CHUNK_BYTES - 1U) / STP_HRT_CHUNK_BYTES);
}

#if !APP_CODEC_ENABLED
static uint16_t chunks_per_frame(void) {
  return chunks_of(APP_FRAME_BYTES);
}
#endif

#if APP_CODEC_ENABLED
/* Cycle counter, for measuring the encode against the DMA deadline. It is
 * free-running once enabled and costs nothing to read. */
static void cycle_counter_enable(void) {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void record_encode_time(uint32_t started) {
  uint32_t elapsed_us = (DWT->CYCCNT - started) / (SystemCoreClock / 1000000U);
  if (elapsed_us > g_health.codec_encode_us_max) {
    g_health.codec_encode_us_max = elapsed_us;
  }
}

/* Begin compressing a frame into the next free slot, choosing between a
 * keyframe and a difference against the previous one.
 *
 * Keyframes go out on a fixed interval so a decoder that missed a packet
 * recovers. What the reference has to match is the last frame the ground
 * decoded, not the frame immediately before this one off the sensor: the link
 * is slower than the sensor, so most captured frames are never sent, and
 * demanding consecutive generations made almost every frame a keyframe.
 *
 * The published frame is deliberately not pinned. Pinning it stopped the
 * parser republishing for as long as the encode ran, and with two slots the
 * encoder runs almost continuously: the capture rate fell from 8.8 frames a
 * second to 6.5, which then capped the link.
 *
 * Reading it unpinned is safe for the same reason the UVC path is, and with
 * more margin. If the parser reclaims this buffer partway through, it begins
 * refilling from row zero at about one row per millisecond, while the encoder
 * is already further down and moving at four. It cannot be overtaken.
 *
 * Should that ever fail, the failure is benign rather than silent: the
 * checksum and the reference copy are both built from the same reads as the
 * encode, so the frame still decodes and verifies, and the worst a reader sees
 * is a seam where two images meet. */
static void encode_begin(const uint16_t *frame, uint32_t generation,
                         bool force_keyframe) {
  bool keyframe = force_keyframe || !codec_reference_valid ||
                  (codec_frames_since_key >= APP_CODEC_GOP);

  uint8_t slot = codec_write;
  codec_slots[slot].generation = generation;
  codec_slots[slot].mode =
      keyframe ? FRAME_CODEC_MODE_INTRA : FRAME_CODEC_MODE_INTER;
  codec_slots[slot].complete = false;
  codec_slots[slot].length = 0U;
  codec_slots[slot].raw_source = NULL;

  codec_encoding = (int8_t)slot;
  codec_source_frame = frame;
  codec_last_generation = generation;
  codec_keyframe = keyframe;

  /* The reference rolls forward in place: the encoder consumes the old value
   * of each pixel and writes the new one as it goes, so what is left behind is
   * exactly the frame that was transmitted. */
  frame_codec_encode_begin(&codec_encoder, frame,
                           keyframe ? NULL : codec_reference,
                           codec_slots[slot].buffer,
                           sizeof(codec_slots[slot].buffer), codec_reference);
}

/* Hand the encoder's slot over to the transmitter. */
static void encode_finish(void) {
  codec_write ^= 1U;
  ++codec_used;
  codec_encoding = -1;
}

/* Fall back to sending the frame exactly as captured.
 *
 * The pin stays on: unlike a compressed frame, this one is read straight out
 * of the capture buffer over every chunk, so it has to stay still until the
 * last one is away. The chunker releases it. The decoder is left without a
 * reference it can trust, so the next frame must be a keyframe. */
static void encode_give_up(void) {
  uint8_t slot = (uint8_t)codec_encoding;
  lepton_capture_hold_for_codec(true);
  codec_slots[slot].mode = FRAME_CODEC_MODE_RAW;
  codec_slots[slot].raw_source = (const uint8_t *)codec_source_frame;
  codec_slots[slot].length = APP_FRAME_BYTES;
  codec_slots[slot].complete = true;
  codec_reference_valid = false;
  health_increment(&g_health.codec_raw_fallback);
  encode_finish();
}

/* Bands of rows, up to the time budget. Called every task iteration, including
 * while a packet is going out, because the transmit is DMA and the core is
 * free meanwhile. */
static void encode_step(void) {
  if (codec_encoding < 0) {
    return;
  }
  uint8_t slot = (uint8_t)codec_encoding;
  uint32_t started = DWT->CYCCNT;
  uint32_t budget = APP_CODEC_STEP_BUDGET_US * (SystemCoreClock / 1000000U);
  size_t length = 0U;
  frame_codec_step_t step = FRAME_CODEC_BUSY;

  while ((DWT->CYCCNT - started) < budget) {
    step = frame_codec_encode_step(&codec_encoder, APP_CODEC_ROWS_PER_STEP,
                                   &length);
    if (step != FRAME_CODEC_BUSY) {
      break;
    }
  }
  record_encode_time(started);

  if (step == FRAME_CODEC_BUSY) {
    return;
  }

  if (step == FRAME_CODEC_OVERFLOW) {
    /* Chunks of the abandoned attempt may already be on the wire. The ground
     * discards a frame whose chunks stop arriving, and the checksum would
     * reject it in any case, so all that matters here is that both ends agree
     * there is no usable reference any more. */
    codec_reference_valid = false;
    if (!codec_keyframe) {
      encode_begin(codec_source_frame, codec_slots[slot].generation, true);
      return;
    }
    encode_give_up();
    return;
  }

  codec_slots[slot].length = length;
  codec_slots[slot].complete = true;
  codec_length = length;
  if (codec_slots[slot].mode == FRAME_CODEC_MODE_INTRA) {
    codec_frames_since_key = 0U;
    health_increment(&g_health.codec_keyframes);
  } else {
    ++codec_frames_since_key;
  }
  codec_reference_valid = true;
  encode_finish();
}

/* Keep the encoder fed: step whatever is in progress, and otherwise start the
 * next frame as soon as a slot is free, without waiting for the previous one
 * to finish transmitting. */
static void codec_task(void) {
  if (codec_encoding >= 0) {
    encode_step();
    return;
  }
  if (!current.hrt_enabled || (codec_used >= 2U)) {
    return;
  }
  uint32_t generation = 0U;
  const uint16_t *frame = lepton_capture_latest_frame(&generation);
  if ((frame == NULL) || (generation == codec_last_generation)) {
    return;  /* nothing new to send yet */
  }
  encode_begin(frame, generation, false);
}

/* Abandon everything in flight, releasing the pin so capture is not stalled. */
static void codec_reset(void) {
  lepton_capture_hold_for_codec(false);
  codec_encoding = -1;
  codec_used = 0U;
  codec_read = 0U;
  codec_write = 0U;
  codec_slots[0].complete = false;
  codec_slots[1].complete = false;
  /* Whatever was half sent is a gap on the ground, so the next frame coded
   * must stand on its own. */
  codec_reference_valid = false;
}
#endif

static bool begin_transmit(size_t length) {
  if (current.transmitting || (length == 0U)) {
    if (length != 0U) {
      health_increment(&g_health.rs485_tx_busy);
    }
    return false;
  }
  /* Drive the transceiver only for the duration of this packet; the callback
   * releases it once the last bit has left the shift register. */
  board_rs485_de(true);
  current.transmitting = true;
  if (HAL_UART_Transmit_DMA(&huart2, tx_buffer, (uint16_t)length) != HAL_OK) {
    current.transmitting = false;
    board_rs485_de(false);
    return false;
  }
  return true;
}

bool stp_link_init(void) {
  memset(&current, 0, sizeof(current));
  /* node_address doubles as the Target ID so a node keeps its identity across
   * power cycles; the stored default matches STP_DEFAULT_TARGET_ID. */
  uint8_t stored = settings_get()->node_address;
  current.target_id = (stored == 0U) ? STP_DEFAULT_TARGET_ID : stored;
  /* Bench builds start streaming; flight waits to be told. Either way the
   * flag is what the task loop obeys, so stop always works. */
  current.hrt_enabled = (STP_BENCH_FREERUN != 0);
  current.capture_state = (uint8_t)(STP_BENCH_FREERUN ? STP_CAPTURE_RECORDING
                                                      : STP_CAPTURE_IDLE);
  pending_ack = false;
  pending_lrt = false;
  burst_active = false;
#if APP_CODEC_ENABLED
  codec_reset();
#endif
  correcting_until_ms = 0U;
  stp_receiver_init(&receiver);
#if APP_CODEC_ENABLED
  cycle_counter_enable();
#endif
  board_rs485_de(false);
  return HAL_UART_Receive_DMA(&huart2, rx_dma, sizeof(rx_dma)) == HAL_OK;
}

/* ------------------------------------------------------------ payloads --- */

static void put_u16(uint8_t *out, uint16_t value) { stp_put_u16(out, value); }
static void put_u32(uint8_t *out, uint32_t value) { stp_put_u32(out, value); }

/* Housekeeping. Offsets are fixed and documented so the ground segment can
 * decode without sharing this header. */
static size_t build_lrt_payload(uint8_t *out, size_t capacity) {
  if (capacity < STP_LRT_PAYLOAD_SIZE) {
    return 0U;
  }
  memset(out, 0, STP_LRT_PAYLOAD_SIZE);

  lepton_capture_status_t camera;
  lepton_capture_get_status(&camera);
  dosimeter_snapshot_t dose;
  dosimeter_get_snapshot(&dose);

  put_u32(&out[0], STP_LRT_LAYOUT_VERSION);
  put_u32(&out[4], HAL_GetTick());
  put_u32(&out[8], current.coarse_time);
  put_u16(&out[12], current.fine_time);
  out[14] = (uint8_t)camera.state;
  out[15] = (uint8_t)(settings_status() & 0xFFU);
  put_u32(&out[16], camera.frame_generation);
  put_u16(&out[20], (uint16_t)camera.last_cci_result);
  put_u16(&out[22], (uint16_t)camera.last_ffc_result);
  put_u32(&out[56], camera.ffc_elapsed_ms);
  out[60] = camera.ffc_shutter_mode;
  out[61] = current.capture_state;
  put_u16(&out[62], (uint16_t)current.images_sent);
#if APP_CODEC_ENABLED
  /* Encoded size of the frame currently going out, so the achieved compression
   * is visible in telemetry rather than only inferable from the packet rate.
   * The mode is not repeated here; it is in every image packet header. */
  put_u16(&out[54], (uint16_t)(codec_length > 0xFFFFU ? 0xFFFFU : codec_length));
#endif

  put_u32(&out[24], (uint32_t)dose.dose_microrad);
  put_u32(&out[28], dose.filtered_voltage_uv);
  put_u32(&out[32], (uint32_t)dose.zero_voltage_uv);
  put_u32(&out[36], dose.flags);
  put_u32(&out[40], dose.vdda_mv);
  put_u16(&out[44], dose.raw_mean);
  put_u16(&out[46], dose.raw_stddev);

  /* Scene temperature summary in centikelvin, matching the pixel units. */
  uint32_t generation = 0U;
  const uint16_t *frame = lepton_capture_latest_frame(&generation);
  if (frame != NULL) {
    uint16_t minimum = frame[0];
    uint16_t maximum = frame[0];
    for (size_t index = 1U; index < APP_FRAME_PIXELS; ++index) {
      if (frame[index] < minimum) {
        minimum = frame[index];
      } else if (frame[index] > maximum) {
        maximum = frame[index];
      }
    }
    put_u16(&out[48], minimum);
    put_u16(&out[50], maximum);
    put_u16(&out[52], frame[(APP_FRAME_HEIGHT / 2U) * APP_FRAME_WIDTH +
                            (APP_FRAME_WIDTH / 2U)]);
  }

  /* The whole health block, so a ground operator sees the same counters the
   * development tooling shows. */
  const uint32_t *counters = (const uint32_t *)&g_health;
  size_t count = sizeof(g_health) / sizeof(uint32_t);
  for (size_t index = 0U; index < count; ++index) {
    put_u32(&out[64U + (index * 4U)], counters[index]);
  }
  return STP_LRT_PAYLOAD_SIZE;
}

/* One slice of the image, with everything the far end needs to reassemble it
 * without holding state across packets.
 *
 * The image is encoded once, at the first chunk, and the remaining chunks are
 * cut from the encoder's output. That is also why a compressed frame cannot
 * tear across chunks the way an uncompressed one could: after the first chunk
 * the sensor buffer is no longer being read. */
static size_t build_hrt_payload(uint8_t *out, size_t capacity) {
  if (capacity < STP_HRT_PAYLOAD_SIZE) {
    return 0U;
  }
  memset(out, 0, STP_HRT_PAYLOAD_SIZE);

  uint32_t generation = 0U;
  const uint16_t *frame = lepton_capture_latest_frame(&generation);
  if (frame == NULL) {
    return 0U;
  }

#if APP_CODEC_ENABLED
  /* A compressed frame may still be encoding while its opening chunks go out,
   * so the total size is not always known. The count is then sent as zero and
   * the last chunk is flagged instead. */
  const codec_slot_t *sending = &codec_slots[codec_read];
  hrt_generation = sending->generation;
  if (sending->complete) {
    hrt_total_chunks = chunks_of(sending->length);
    hrt_length = sending->length;
  } else {
    hrt_total_chunks = 0U;
    hrt_length = frame_codec_encode_available(&codec_encoder);
  }
#else
  if (hrt_chunk == 0U) {
    hrt_generation = generation;
    hrt_total_chunks = chunks_per_frame();
    hrt_length = APP_FRAME_BYTES;
  }
#endif

#if APP_CODEC_ENABLED
  const uint8_t *source = (sending->raw_source != NULL) ? sending->raw_source
                                                        : sending->buffer;
  uint8_t mode = sending->mode;
#else
  const uint8_t *source = (const uint8_t *)frame;
  uint8_t mode = FRAME_CODEC_MODE_RAW;
#endif

  uint32_t offset = (uint32_t)hrt_chunk * STP_HRT_CHUNK_BYTES;
  if ((uint32_t)hrt_length <= offset) {
    health_increment(&g_health.codec_chunk_starved);
    return 0U;  /* the encoder has not produced this chunk yet */
  }
  uint32_t remaining = (uint32_t)hrt_length - offset;
  uint16_t length = remaining < STP_HRT_CHUNK_BYTES ? (uint16_t)remaining
                                                    : STP_HRT_CHUNK_BYTES;
#if APP_CODEC_ENABLED
  /* Only a complete chunk may go out early; a short one is the end of the
   * frame, which is only true once the encoder has finished. */
  if (!sending->complete && (length < STP_HRT_CHUNK_BYTES)) {
    health_increment(&g_health.codec_chunk_starved);
    return 0U;
  }
  bool final_chunk = sending->complete && (remaining <= STP_HRT_CHUNK_BYTES);
#else
  bool final_chunk = (remaining <= STP_HRT_CHUNK_BYTES);
#endif

  put_u32(&out[0], hrt_generation);
  put_u16(&out[4], hrt_chunk);
  put_u16(&out[6], hrt_total_chunks);
  put_u32(&out[8], offset);
  put_u16(&out[12], length);
  /* Version 1 put a 16-bit version here, so its high byte was always zero.
   * Splitting the field keeps the header at 16 bytes and lets a decoder tell
   * the two apart. */
  out[14] = (uint8_t)STP_HRT_LAYOUT_VERSION;
  out[15] = (uint8_t)(mode | (final_chunk ? STP_HRT_MODE_FINAL : 0U));
  memcpy(&out[STP_HRT_HEADER_SIZE], source + offset, length);

  ++hrt_chunk;
  if (final_chunk) {
    hrt_chunk = 0U;
#if APP_CODEC_ENABLED
    /* A raw frame is read from the capture buffer, so the pin is only released
     * once its last chunk is away. */
    if (sending->raw_source != NULL) {
      lepton_capture_hold_for_codec(false);
    }
    codec_read ^= 1U;
    --codec_used;
#endif
  }
  return STP_HRT_PAYLOAD_SIZE;
}

/* -------------------------------------------------------------- sending -- */

static bool send_ack(void) {
  return begin_transmit(stp_build_ack(tx_buffer, sizeof(tx_buffer),
                                      current.target_id));
}

static bool send_lrt(void) {
  uint8_t payload[STP_LRT_PAYLOAD_SIZE];
  size_t length = build_lrt_payload(payload, sizeof(payload));
  if (length == 0U) {
    return false;
  }
  if (!begin_transmit(stp_build_lrt_data(tx_buffer, sizeof(tx_buffer),
                                         current.target_id, payload, length))) {
    return false;
  }
  ++current.lrt_sent;
  return true;
}

static bool send_hrt(void) {
  uint8_t payload[STP_HRT_PAYLOAD_SIZE];
  size_t length = build_hrt_payload(payload, sizeof(payload));
  if (length == 0U) {
    return false;
  }
  if (!begin_transmit(stp_build_hrt_data(tx_buffer, sizeof(tx_buffer),
                                         current.target_id, payload, length))) {
    return false;
  }
  ++current.hrt_sent;
  /* build_hrt_payload wraps the chunk index back to zero after the last slice,
   * so this is where a whole frame has just gone out. */
  if ((hrt_chunk == 0U) && burst_active) {
    burst_active = false;
    current.hrt_enabled = false;
    current.capture_state = (uint8_t)STP_CAPTURE_IDLE;
    ++current.images_sent;
  }
  return true;
}

/* ------------------------------------------------------------- commands -- */

/* Begin a capture by correcting the image first. The stream stays off until
 * the shutter has finished and the first frames after it have settled. */
static void begin_corrected_capture(stp_capture_state_t target) {
  (void)lepton_capture_run_ffc();
  correcting_until_ms = HAL_GetTick() + APP_LEPTON_FFC_SETTLE_MS;
  current.capture_state = (uint8_t)STP_CAPTURE_CORRECTING;
  current.hrt_enabled = false;
  burst_active = (target == STP_CAPTURE_SINGLE);
  hrt_chunk = 0U;
  /* Remembered so the settle timer knows what to start. */
  pending_capture_target = (uint8_t)target;
}

static void stop_capture(void) {
  current.hrt_enabled = false;
  burst_active = false;
  correcting_until_ms = 0U;
  current.capture_state = (uint8_t)STP_CAPTURE_IDLE;
  hrt_chunk = 0U;
#if APP_CODEC_ENABLED
  /* Abandon anything in flight, or capture stalls for as long as the stream
   * stays stopped. */
  codec_reset();
#endif
}

static void execute_command(uint8_t command) {
  switch (command) {
    case STP_CMD_RUN_FFC:
      (void)lepton_capture_run_ffc();
      break;
    case STP_CMD_TAKE_IMAGE:
      begin_corrected_capture(STP_CAPTURE_SINGLE);
      break;
    case STP_CMD_START_RECORD:
      begin_corrected_capture(STP_CAPTURE_RECORDING);
      break;
    case STP_CMD_STOP_RECORD:
    case STP_CMD_STREAM_OFF:
      stop_capture();
      break;
    case STP_CMD_STREAM_ON:
      /* Deliberately no correction: the caller is asking for the image as it
       * stands, which is what you want when it is already settled. */
      burst_active = false;
      correcting_until_ms = 0U;
      current.hrt_enabled = true;
      current.capture_state = (uint8_t)STP_CAPTURE_RECORDING;
      break;
    case STP_CMD_DOSIMETER_ZERO:
      (void)dosimeter_begin_zero();
      break;
    default:
      /* Unknown command: still acknowledged, but nothing is done. */
      return;
  }
  ++current.commands_executed;
}

/* ------------------------------------------------------------ receiving -- */

static void handle_packet(const stp_rx_packet_t *packet) {
  if (packet->target_id != current.target_id) {
    /* Traffic for another experiment: count it and stay silent. */
    ++current.rejected_target;
    return;
  }

  if ((packet->kind == STP_RX_COMMAND) || (packet->kind == STP_RX_LRT_REQUEST)) {
    current.coarse_time = packet->coarse_time;
    current.fine_time = packet->fine_time;
  }

  switch (packet->kind) {
    case STP_RX_COMMAND:
      ++current.commands_received;
      if (packet->payload != NULL) {
        execute_command(packet->payload[0]);
      }
      pending_ack = true;
      break;
    case STP_RX_LRT_REQUEST:
      ++current.lrt_requests;
      pending_lrt = true;
      break;
    case STP_RX_HRT_GO:
      current.hrt_enabled = true;
      current.capture_state = (uint8_t)STP_CAPTURE_RECORDING;
      break;
    case STP_RX_HRT_STOP:
    case STP_RX_HRT_STOP_WITH_LOSS:
      /* Restart at a frame boundary when it resumes. Stop with loss differs
       * only in that DICE expects the gap, so both are handled alike here. */
      stop_capture();
      break;
    default:
      break;
  }
}

void stp_link_task(void) {
  uint16_t write_position =
      (uint16_t)(STP_RX_DMA_SIZE - __HAL_DMA_GET_COUNTER(huart2.hdmarx));
  while (rx_position != write_position) {
    uint8_t byte = rx_dma[rx_position++];
    if (rx_position == STP_RX_DMA_SIZE) {
      rx_position = 0U;
    }
    stp_rx_packet_t packet;
    if (stp_receiver_push(&receiver, byte, &packet)) {
      handle_packet(&packet);
    }
  }
  current.crc_errors = receiver.crc_errors;
  current.type_errors = receiver.type_errors;

#if APP_CODEC_ENABLED
  /* Compression runs whether or not a packet is going out: the transmit is
   * DMA, so the core is free while it drains. */
  codec_task();
#endif

  if (current.transmitting) {
    return;
  }
  uint32_t now = HAL_GetTick();

  /* A capture waiting on the shutter starts once it has settled. */
  if ((correcting_until_ms != 0U) &&
      ((int32_t)(now - correcting_until_ms) >= 0)) {
    correcting_until_ms = 0U;
    hrt_chunk = 0U;
    current.hrt_enabled = true;
    current.capture_state = pending_capture_target;
  }

  /* Owed replies go first: DICE asked for these and is waiting. */
  if (pending_ack) {
    if (send_ack()) {
      pending_ack = false;
    }
    return;
  }
  if (pending_lrt) {
    if (send_lrt()) {
      pending_lrt = false;
      last_lrt_ms = now;
    }
    return;
  }

  /* Then unsolicited vitals, so image traffic cannot starve them. */
#if STP_BENCH_FREERUN
  if ((now - last_lrt_ms) >= STP_FREERUN_LRT_PERIOD_MS) {
    last_lrt_ms = now;
    (void)send_lrt();
    return;
  }
#endif

  /* Whatever link is left carries the image, while the stream is enabled. */
#if APP_CODEC_ENABLED
  bool image_ready = (codec_used > 0U);
#else
  bool image_ready = true;
#endif
  if (current.hrt_enabled && image_ready &&
      ((now - last_hrt_ms) >= STP_HRT_MIN_GAP_MS)) {
    last_hrt_ms = now;
    (void)send_hrt();
  }
}

bool stp_link_set_target_id(uint8_t target_id) {
  if ((target_id == 0U) || (target_id == 0xFFU)) {
    return false;
  }
  current.target_id = target_id;
  (void)settings_set_node_address(target_id);
  return true;
}

void stp_link_get_status(stp_link_status_t *status) {
  if (status != NULL) {
    __disable_irq();
    *status = current;
    __enable_irq();
  }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *handle) {
  if (handle->Instance == USART2) {
    /* Release the driver as soon as transmission completes so the bus is free
     * for DICE and the other experiments. */
    board_rs485_de(false);
    current.transmitting = false;
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *handle) {
  if (handle->Instance == USART2) {
    board_rs485_de(false);
    current.transmitting = false;
    health_increment(&g_health.rs485_rx_overruns);
    (void)HAL_UART_Receive_DMA(&huart2, rx_dma, sizeof(rx_dma));
    rx_position = 0U;
    stp_receiver_init(&receiver);
  }
}
