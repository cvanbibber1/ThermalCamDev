#include "lepton_capture.h"

#include "app_config.h"
#include "board.h"
#include "health.h"
#include "lepton_cci.h"
#include "vospi_parser.h"

#include <string.h>

#define VOSPI_CHUNK_SIZE (2U * VOSPI_SEGMENT_SIZE)
/* A segment can start anywhere in the previous chunk, so the search needs the
 * previous chunk's trailing bytes to stay contiguous with the current chunk.
 * Only the last VOSPI_SEGMENT_SIZE - 1 bytes can still hold an unprocessed
 * start, so carrying that tail replaces the previous full-chunk history copy. */
#define VOSPI_CARRY_SIZE (VOSPI_SEGMENT_SIZE - 1U)
#define VOSPI_WINDOW_SIZE (VOSPI_CARRY_SIZE + VOSPI_CHUNK_SIZE)

static vospi_parser_t parser;
_Alignas(4) volatile uint8_t vospi_diagnostic_raw[2][VOSPI_CHUNK_SIZE];
_Alignas(4) static uint8_t scan_window[VOSPI_WINDOW_SIZE];
/* Consecutive chunks with no recognizable segment before the bit phase is
 * re-derived rather than trusted. */
#define VOSPI_PHASE_RETRY_CHUNKS 8U
/* Non-discard packets a candidate phase must number consecutively. */
#define VOSPI_PHASE_CONFIRM_PACKETS 8U
/* Packets examined while validating one candidate phase. */
#define VOSPI_PHASE_PROBE_PACKETS 40U

static uint8_t spi_tx_zero;
static volatile bool transfer_ready;
static volatile bool spi_running;
static volatile uint8_t completed_buffer;
static uint8_t active_buffer;
static bool scan_primed;
/* SPI2 samples the Lepton one bit late, so the captured byte stream is a
 * right-shifted copy of the wire packets. The shift is derived from the data
 * instead of assumed, which also keeps this correct if the extra clock edge is
 * ever eliminated in hardware (the detected phase is then simply zero). */
static uint8_t bit_phase;
static bool bit_phase_valid;
static uint8_t shift_carry;
static uint8_t barren_chunks;
static uint32_t hold_started_ms;
static uint32_t last_ffc_check_ms;
static bool frame_held;
static lepton_capture_status_t capture_status;
static uint32_t state_started_ms;
static uint32_t last_boot_poll_ms;
static volatile uint32_t chunk_ready_ms;
volatile uint32_t vospi_diagnostic_packets;
volatile int32_t vospi_diagnostic_result;

static void enter_state(lepton_state_t state) {
  capture_status.state = state;
  state_started_ms = HAL_GetTick();
}

static void stop_spi_and_resync(void) {
  (void)HAL_SPI_Abort(&hspi2);
  spi_running = false;
  transfer_ready = false;
  board_lepton_cs(false);
  board_lepton_spi_clock_hold();
  /* Preserve the last fully published frame across link reacquisition. */
  parser.expected_segment = 1U;
  scan_primed = false;
  bit_phase_valid = false;
  barren_chunks = 0U;
  health_increment(&g_health.vospi_resyncs);
  enter_state(LEPTON_STATE_RESYNC);
}

/* One circular transfer spans both chunk buffers: the half-transfer interrupt
 * hands over buffer 0 while the DMA fills buffer 1, and the transfer-complete
 * interrupt does the reverse. Clocking is continuous for as long as the link
 * is up, so there is no per-chunk restart to get wrong. */
static bool start_stream(void) {
  active_buffer = 0U;
  if (HAL_SPI_TransmitReceive_DMA(&hspi2, &spi_tx_zero,
                                  (uint8_t *)&vospi_diagnostic_raw[0][0],
                                  2U * VOSPI_CHUNK_SIZE) != HAL_OK) {
    return false;
  }
  spi_running = true;
  chunk_ready_ms = HAL_GetTick();
  return true;
}

void lepton_capture_init(void) {
  memset(&capture_status, 0, sizeof(capture_status));
  vospi_parser_init(&parser);
  board_lepton_cs(false);
  board_lepton_spi_clock_hold();
  board_lepton_reset(false);
  board_lepton_power(false);
  enter_state(LEPTON_STATE_POWER_OFF);
}

static void handle_result(vospi_result_t result) {
  if (result == VOSPI_RESULT_SEGMENT) {
    health_increment(&g_health.vospi_segments);
  } else if (result == VOSPI_RESULT_IGNORED) {
    health_increment(&g_health.vospi_segments_ignored);
  }
  if (result == VOSPI_RESULT_FRAME) {
    health_increment(&g_health.vospi_segments);
    capture_status.frame_generation = parser.generation;
    health_increment(&g_health.frames_complete);
  } else if (result == VOSPI_RESULT_CRC_ERROR) {
    health_increment(&g_health.vospi_crc_errors);
    stop_spi_and_resync();
  } else if (result == VOSPI_RESULT_SEQUENCE_ERROR) {
    health_increment(&g_health.vospi_sequence_errors);
    stop_spi_and_resync();
  }
}

/* Byte `index` of `raw` as it appears after right-shifting the bit stream by
 * `shift`. Index zero is never requested with a non-zero shift. */
static uint8_t shifted_byte(const uint8_t *raw, size_t index, uint8_t shift) {
  if (shift == 0U) {
    return raw[index];
  }
  return (uint8_t)((raw[index - 1U] << (8U - shift)) | (raw[index] >> shift));
}

/* VoSPI packets are contiguous, so a packet boundary exists within the first
 * VOSPI_PACKET_SIZE bytes for exactly one (bit shift, byte offset) pair. A
 * candidate is accepted only when consecutively numbered packets appear at the
 * 164-byte stride, which discard traffic cannot imitate. */
static bool detect_bit_phase(const uint8_t *raw, size_t length, uint8_t *phase) {
  for (uint8_t shift = 0U; shift < 8U; ++shift) {
    for (size_t offset = 1U; offset <= VOSPI_PACKET_SIZE; ++offset) {
      uint16_t previous = 0U;
      bool have_previous = false;
      uint16_t confirmed = 0U;
      bool candidate_ok = true;
      for (uint16_t index = 0U; index < VOSPI_PHASE_PROBE_PACKETS; ++index) {
        size_t position = offset + ((size_t)index * VOSPI_PACKET_SIZE);
        if ((position + 1U) >= length) {
          break;
        }
        uint8_t high = shifted_byte(raw, position, shift);
        uint8_t low = shifted_byte(raw, position + 1U, shift);
        if ((high & 0x0FU) == 0x0FU) {
          have_previous = false;
          continue;
        }
        uint16_t number = (uint16_t)((uint16_t)(high & 0x0FU) << 8) | low;
        if (number >= VOSPI_PACKETS_PER_SEGMENT) {
          candidate_ok = false;
          break;
        }
        if (have_previous &&
            (number != ((previous + 1U) % VOSPI_PACKETS_PER_SEGMENT))) {
          candidate_ok = false;
          break;
        }
        previous = number;
        have_previous = true;
        ++confirmed;
      }
      if (candidate_ok && (confirmed >= VOSPI_PHASE_CONFIRM_PACKETS)) {
        *phase = shift;
        return true;
      }
    }
  }
  return false;
}

/* Right-shift the freshly copied chunk in place. Walking backwards keeps the
 * still-unshifted predecessor available for each byte, and the raw byte that
 * preceded the chunk arrives in shift_carry. */
static void apply_bit_phase(uint8_t *chunk, size_t length, uint8_t shift,
                            uint8_t carry) {
  if (shift == 0U) {
    return;
  }
  for (size_t index = length - 1U; index > 0U; --index) {
    chunk[index] =
        (uint8_t)((chunk[index - 1U] << (8U - shift)) | (chunk[index] >> shift));
  }
  chunk[0] = (uint8_t)((carry << (8U - shift)) | (chunk[0] >> shift));
}

static void process_chunk(uint8_t buffer) {
  /* The carry already carries the previous chunk's shift, so only the new
   * bytes are converted and the joined window stays uniformly aligned. */
  memmove(scan_window, &scan_window[VOSPI_CHUNK_SIZE], VOSPI_CARRY_SIZE);
  uint8_t *chunk = &scan_window[VOSPI_CARRY_SIZE];
  memcpy(chunk, (const uint8_t *)vospi_diagnostic_raw[buffer],
         VOSPI_CHUNK_SIZE);
  uint8_t next_carry = chunk[VOSPI_CHUNK_SIZE - 1U];
  health_increment(&g_health.vospi_chunks);

  if (!bit_phase_valid) {
    uint8_t detected = 0U;
    if (!detect_bit_phase(chunk, VOSPI_CHUNK_SIZE, &detected)) {
      shift_carry = next_carry;
      scan_primed = false;
      health_increment(&g_health.vospi_discard_packets);
      return;
    }
    bit_phase = detected;
    bit_phase_valid = true;
    barren_chunks = 0U;
    /* The carry still holds the previous phase, so rebuild the window. */
    scan_primed = false;
  }

  apply_bit_phase(chunk, VOSPI_CHUNK_SIZE, bit_phase, shift_carry);
  shift_carry = next_carry;
  if (!scan_primed) {
    scan_primed = true;
    return;
  }

  bool found = false;
  const uint8_t *window = scan_window;
  for (size_t start = 0U; start <= (VOSPI_WINDOW_SIZE - VOSPI_SEGMENT_SIZE);
       ++start) {
    const uint8_t *candidate = &window[start];
    if (((candidate[0] & 0x0FU) != 0U) || candidate[1] != 0U) {
      continue;
    }
    bool valid = true;
    for (uint16_t packet = 1U; packet < VOSPI_PACKETS_PER_SEGMENT; ++packet) {
      const uint8_t *header = &candidate[(size_t)packet * VOSPI_PACKET_SIZE];
      uint16_t packet_number =
          ((uint16_t)(header[0] & 0x0FU) << 8) | header[1];
      if (((header[0] & 0x0FU) == 0x0FU) || packet_number != packet) {
        valid = false;
        break;
      }
    }
    if (!valid) {
      continue;
    }
    found = true;
    handle_result(vospi_parse_segment(&parser, candidate, true));
    if (capture_status.state == LEPTON_STATE_RESYNC) {
      return;
    }
    start += VOSPI_SEGMENT_SIZE - 1U;
  }
  if (found) {
    barren_chunks = 0U;
  } else {
    health_increment(&g_health.vospi_discard_packets);
    ++barren_chunks;
    if (barren_chunks >= VOSPI_PHASE_RETRY_CHUNKS) {
      /* Re-derive the alignment before escalating to a full reacquisition. */
      bit_phase_valid = false;
      barren_chunks = 0U;
    }
  }
}

/* Supervise flat-field correction.
 *
 * The camera performs FFC itself on its own schedule, so the firmware only
 * confirms that it is still doing so. A module left in manual shutter mode, or
 * one whose correction is overdue (temperature lockout is the usual reason),
 * gets an explicit RUN FFC instead - the image drifts visibly within a few
 * minutes without one. */
static int check_ffc_policy(uint32_t now, bool force_check) {
  if (!force_check && ((now - last_ffc_check_ms) < APP_LEPTON_FFC_CHECK_MS)) {
    return capture_status.last_ffc_result;
  }
  last_ffc_check_ms = now;

  lepton_ffc_status_t ffc;
  int result = lepton_cci_get_ffc_status(&ffc);
  if (result != LEPTON_CCI_OK) {
    return result;
  }

  uint32_t deadline = ffc.desired_period_ms + APP_LEPTON_FFC_OVERDUE_MS;
  bool overdue = (ffc.desired_period_ms != 0U) &&
                 (ffc.elapsed_since_ffc_ms > deadline);
  if ((ffc.shutter_mode != LEPTON_FFC_SHUTTER_MODE_AUTO) || overdue) {
    result = lepton_cci_run(LEPTON_CID_SYS_RUN_FFC);
    if (result == LEPTON_CCI_OK) {
      health_increment(&g_health.ffc_forced_runs);
    }
  }
  return result;
}

void lepton_capture_task(void) {
  uint32_t now = HAL_GetTick();
  if (frame_held && ((now - hold_started_ms) >= APP_FRAME_HOLD_MS)) {
    lepton_capture_hold_frame(false);
  }
  switch (capture_status.state) {
    case LEPTON_STATE_POWER_OFF:
      if ((now - state_started_ms) >= 150U) {
        board_lepton_power(true);
        enter_state(LEPTON_STATE_RESET_HOLD);
      }
      break;
    case LEPTON_STATE_RESET_HOLD:
      if ((now - state_started_ms) >= 10U) {
        board_lepton_reset(true);
        enter_state(LEPTON_STATE_BOOTING);
      }
      break;
    case LEPTON_STATE_BOOTING:
      if (((now - state_started_ms) >= APP_LEPTON_CCI_EARLIEST_MS) &&
          ((now - last_boot_poll_ms) >= 50U)) {
        last_boot_poll_ms = now;
        if (lepton_cci_booted()) {
          enter_state(LEPTON_STATE_CONFIGURING);
        }
      }
      if ((now - state_started_ms) >= APP_LEPTON_BOOT_DEADLINE_MS) {
        health_increment(&g_health.camera_boot_failures);
        enter_state(LEPTON_STATE_RETRY);
      }
      break;
    case LEPTON_STATE_CONFIGURING:
      capture_status.last_cci_result = lepton_cci_configure_radiometric();
      if (capture_status.last_cci_result == LEPTON_CCI_OK) {
        /* Flat-field policy is not required for imaging: if the camera refuses
         * it the picture drifts but still streams, so record the failure and
         * carry on rather than power-cycling into a retry loop. */
        capture_status.last_ffc_result = (int16_t)check_ffc_policy(now, true);
        board_lepton_cs(false);
        enter_state(LEPTON_STATE_RESYNC);
      } else {
        enter_state(LEPTON_STATE_RETRY);
      }
      break;
    case LEPTON_STATE_WAIT_VSYNC:
      enter_state(LEPTON_STATE_RESYNC);
      break;
    case LEPTON_STATE_STREAMING:
      if (transfer_ready) {
        __disable_irq();
        uint8_t done = completed_buffer;
        transfer_ready = false;
        __enable_irq();
        process_chunk(done);
      } else if (!spi_running) {
        health_increment(&g_health.vospi_link_stalls);
        stop_spi_and_resync();
      } else if ((now - chunk_ready_ms) >= APP_LEPTON_CHUNK_TIMEOUT_MS) {
        /* Circular DMA never reports completion on its own, so a transport
         * that stopped delivering halves is only visible as silence. */
        health_increment(&g_health.vospi_link_stalls);
        stop_spi_and_resync();
      } else {
        /* Only between chunks: the CCI read blocks for several milliseconds
         * and a chunk buffer waiting to be parsed must not be delayed. */
        capture_status.last_ffc_result = (int16_t)check_ffc_policy(now, false);
      }
      break;
    case LEPTON_STATE_RESYNC:
      if ((now - state_started_ms) >= APP_LEPTON_RESYNC_MS) {
        transfer_ready = false;
        scan_primed = false;
        vospi_diagnostic_packets = 0U;
        vospi_diagnostic_result = HAL_BUSY;
        board_lepton_spi_clock_enable();
        __HAL_SPI_ENABLE(&hspi2);
        board_lepton_cs(true);
        if (start_stream()) {
          enter_state(LEPTON_STATE_STREAMING);
        } else {
          health_increment(&g_health.vospi_start_failures);
          stop_spi_and_resync();
        }
      }
      break;
    case LEPTON_STATE_RETRY:
      board_lepton_cs(false);
      board_lepton_reset(false);
      board_lepton_power(false);
      if ((now - state_started_ms) >= 1000U) {
        enter_state(LEPTON_STATE_POWER_OFF);
      }
      break;
    default:
      enter_state(LEPTON_STATE_RETRY);
      break;
  }
}

const uint16_t *lepton_capture_latest_frame(uint32_t *generation) {
  return vospi_latest_frame(&parser, generation);
}

void lepton_capture_get_status(lepton_capture_status_t *status) {
  if (status != NULL) {
    __disable_irq();
    *status = capture_status;
    __enable_irq();
  }
}

void lepton_capture_hold_frame(bool hold) {
  if (hold && !frame_held) {
    hold_started_ms = HAL_GetTick();
  }
  frame_held = hold;
  vospi_set_hold(&parser, hold);
}

int lepton_capture_run_ffc(void) {
  return lepton_cci_run(LEPTON_CID_SYS_RUN_FFC);
}

void HAL_GPIO_EXTI_Callback(uint16_t pin) {
  if (pin == GPIO_PIN_13) {
    capture_status.last_vsync_ms = HAL_GetTick();
  }
}

/* Buffer 0 is complete once the circular transfer reaches its midpoint. */
static void publish_chunk(uint8_t buffer) {
  vospi_diagnostic_result = HAL_OK;
  vospi_diagnostic_packets += 2U * VOSPI_PACKETS_PER_SEGMENT;
  chunk_ready_ms = HAL_GetTick();
  if (transfer_ready) {
    /* The task loop did not consume the previous half in time and the DMA has
     * already overwritten it. */
    health_increment(&g_health.frames_dropped);
  }
  active_buffer = buffer;
  completed_buffer = buffer;
  transfer_ready = true;
}

void HAL_SPI_TxRxHalfCpltCallback(SPI_HandleTypeDef *handle) {
  if (handle->Instance == SPI2) {
    publish_chunk(0U);
  }
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *handle) {
  if (handle->Instance == SPI2) {
    publish_chunk(1U);
  }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *handle) {
  if (handle->Instance == SPI2) {
    spi_running = false;
    board_lepton_cs(false);
    health_increment(&g_health.vospi_spi_errors);
  }
}
