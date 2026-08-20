#pragma once

#include <stdbool.h>
#include <stdint.h>

/* RS-422 flight link: the STP/DICE packet protocol on USART2 through the
 * ADM2582E, replacing the development COBS transport on this branch.
 *
 * The experiment is a slave. DICE sends commands, LRT requests and HRT flow
 * control; this side acknowledges commands, answers LRT requests with
 * housekeeping, and streams thermal image chunks as HRT while HRT is enabled.
 */

/* Our own LRT and HRT payload layouts. The specification fixes the packet
 * envelope but says nothing about payload contents, so these must be agreed
 * with DICE before flight. Bumped whenever a field moves. */
#define STP_LRT_LAYOUT_VERSION 1U
#define STP_HRT_LAYOUT_VERSION 1U

/* HRT payload is 1280 bytes; the first 16 carry the reassembly header. */
#define STP_HRT_HEADER_SIZE 16U
#define STP_HRT_CHUNK_BYTES (1280U - STP_HRT_HEADER_SIZE)

/* What the image stream is currently doing, reported in vitals so a ground
 * operator can see whether a request was taken up. */
typedef enum {
  STP_CAPTURE_IDLE = 0,
  STP_CAPTURE_CORRECTING,  /* waiting for the shutter before capturing */
  STP_CAPTURE_SINGLE,      /* sending exactly one frame */
  STP_CAPTURE_RECORDING,   /* streaming until stopped */
} stp_capture_state_t;

typedef struct {
  uint8_t target_id;
  bool hrt_enabled;
  bool transmitting;
  uint32_t commands_received;
  uint32_t lrt_requests;
  uint32_t lrt_sent;
  uint32_t hrt_sent;
  uint32_t rejected_target;
  uint32_t crc_errors;
  uint32_t type_errors;
  /* Latched from the most recent timestamped packet so transmitted telemetry
   * can be correlated with DICE's clock. */
  uint32_t coarse_time;
  uint16_t fine_time;
  uint8_t capture_state;
  uint32_t commands_executed;
  uint32_t images_sent;
} stp_link_status_t;

bool stp_link_init(void);
void stp_link_task(void);
void stp_link_get_status(stp_link_status_t *status);

/* Assign this experiment's Target ID and persist it, so a node keeps its
 * identity across power cycles. The specification does not allocate IDs, so
 * any value except the reserved 0 and 0xFF is accepted. */
bool stp_link_set_target_id(uint8_t target_id);
