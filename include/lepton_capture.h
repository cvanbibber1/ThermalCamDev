#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  LEPTON_STATE_POWER_OFF = 0,
  LEPTON_STATE_RESET_HOLD,
  LEPTON_STATE_BOOTING,
  LEPTON_STATE_CONFIGURING,
  LEPTON_STATE_WAIT_VSYNC,
  LEPTON_STATE_STREAMING,
  LEPTON_STATE_RESYNC,
  LEPTON_STATE_RETRY,
} lepton_state_t;

typedef struct {
  lepton_state_t state;
  int16_t last_cci_result;
  int16_t last_ffc_result;
  uint32_t frame_generation;
  uint32_t last_vsync_ms;
} lepton_capture_status_t;

void lepton_capture_init(void);
void lepton_capture_task(void);
const uint16_t *lepton_capture_latest_frame(uint32_t *generation);
void lepton_capture_get_status(lepton_capture_status_t *status);
int lepton_capture_run_ffc(void);
/* Pin the published frame and its generation for a chunked reader.
 *
 * A frame command is answered over many round trips - about 500 ms on the
 * 921600 baud field bus - while assembly republishes every 114 ms, so an
 * unpinned reader is handed a different image partway through and the transfer
 * fails as stale. Released automatically after APP_FRAME_HOLD_MS so an
 * abandoned transfer cannot stall the video. The UVC path does not use this;
 * see the note in usb_video_if.c. */
void lepton_capture_hold_frame(bool hold);

