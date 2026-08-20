#include "stp_link.h"

#include "app_config.h"
#include "board.h"
#include "dosimeter.h"
#include "health.h"
#include "lepton_capture.h"
#include "protocol/stp_protocol.h"
#include "settings.h"

#include <string.h>

/* Bench bring-up: emit LRT housekeeping periodically and stream HRT without
 * waiting for DICE, so the link can be proven with nothing but an RS-422 to
 * USB converter on the other end.
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
static uint32_t last_lrt_ms;
static uint32_t last_hrt_ms;

static uint16_t chunks_per_frame(void) {
  return (uint16_t)((APP_FRAME_BYTES + STP_HRT_CHUNK_BYTES - 1U) /
                    STP_HRT_CHUNK_BYTES);
}

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
  stp_receiver_init(&receiver);
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

/* One slice of the published frame, with everything the far end needs to
 * reassemble it without holding state across packets. */
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
  /* Start a new frame only at a chunk boundary so a reassembled image never
   * mixes two generations. */
  if (hrt_chunk == 0U) {
    hrt_generation = generation;
  }

  uint16_t total_chunks = chunks_per_frame();
  uint32_t offset = (uint32_t)hrt_chunk * STP_HRT_CHUNK_BYTES;
  uint32_t remaining = APP_FRAME_BYTES - offset;
  uint16_t length = remaining < STP_HRT_CHUNK_BYTES ? (uint16_t)remaining
                                                    : STP_HRT_CHUNK_BYTES;

  put_u32(&out[0], hrt_generation);
  put_u16(&out[4], hrt_chunk);
  put_u16(&out[6], total_chunks);
  put_u32(&out[8], offset);
  put_u16(&out[12], length);
  put_u16(&out[14], STP_HRT_LAYOUT_VERSION);
  memcpy(&out[STP_HRT_HEADER_SIZE], ((const uint8_t *)frame) + offset, length);

  ++hrt_chunk;
  if (hrt_chunk >= total_chunks) {
    hrt_chunk = 0U;
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
  return true;
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
      /* The command payload structure is not defined by the specification, so
       * nothing is decoded from it yet; the packet is acknowledged as
       * received. */
      (void)send_ack();
      break;
    case STP_RX_LRT_REQUEST:
      ++current.lrt_requests;
      (void)send_lrt();
      break;
    case STP_RX_HRT_GO:
      current.hrt_enabled = true;
      break;
    case STP_RX_HRT_STOP:
    case STP_RX_HRT_STOP_WITH_LOSS:
      current.hrt_enabled = false;
      /* Restart at a frame boundary when it resumes. */
      hrt_chunk = 0U;
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

  if (current.transmitting) {
    return;
  }
  uint32_t now = HAL_GetTick();

  /* Vitals take the transmitter first so image traffic cannot starve them. */
#if STP_BENCH_FREERUN
  if ((now - last_lrt_ms) >= STP_FREERUN_LRT_PERIOD_MS) {
    last_lrt_ms = now;
    (void)send_lrt();
    return;
  }
#endif

  /* Then fill the remaining link with image packets. */
#if STP_BENCH_FREERUN
  const bool stream = true;
#else
  const bool stream = current.hrt_enabled;
#endif
  if (stream && ((now - last_hrt_ms) >= STP_HRT_MIN_GAP_MS)) {
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
