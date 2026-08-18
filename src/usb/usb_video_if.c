#include "usb_video_if.h"

#include "app_config.h"
#include "lepton_capture.h"

#include <string.h>

static uint16_t empty_frame[APP_FRAME_PIXELS];
static const uint8_t *active_frame = (const uint8_t *)empty_frame;
static uint16_t packet_index;

static int8_t video_init(void) {
  packet_index = 0U;
  return 0;
}

static int8_t video_deinit(void) {
  return 0;
}

static int8_t video_control(uint8_t command, uint8_t *buffer, uint16_t length) {
  (void)command;
  (void)buffer;
  (void)length;
  return 0;
}

static int8_t video_data(uint8_t **buffer, uint16_t *size, uint16_t *index) {
  const uint16_t payload_capacity = UVC_PACKET_SIZE - 2U;
  const uint16_t full_packets = APP_FRAME_BYTES / payload_capacity;
  const uint16_t remainder = APP_FRAME_BYTES % payload_capacity;

  if (packet_index == 0U) {
    /* The published frame is latched for the whole payload but deliberately
     * not pinned. Pinning would stall publication: a payload occupies 77 of
     * every 78 ms, leaving assembly almost no unpinned window to publish in,
     * which drops the delivered rate from 8.8 to 0.1 unique frames per second.
     *
     * Reading an unpinned buffer is safe because both sides walk the frame
     * top to bottom and the reader is strictly faster: UVC drains 120 rows in
     * about 77 ms while assembly fills them in about 114 ms, so once the
     * parser reclaims this buffer its write position stays behind the read
     * position. Measured over 525 consecutive frames with no torn frame.
     *
     * This holds only while a payload completes inside one camera frame
     * period. Raising the frame rate, enlarging the frame, or a host that
     * stalls the isochronous pipe past 114 ms would reintroduce tearing and
     * would need a third assembly buffer rather than a hold. */
    uint32_t generation;
    const uint16_t *latest = lepton_capture_latest_frame(&generation);
    (void)generation;
    active_frame = latest == NULL ? (const uint8_t *)empty_frame : (const uint8_t *)latest;
  }

  *index = packet_index;
  if (packet_index < full_packets) {
    *buffer = (uint8_t *)&active_frame[(size_t)packet_index * payload_capacity];
    *size = UVC_PACKET_SIZE;
    ++packet_index;
  } else if ((packet_index == full_packets) && (remainder != 0U)) {
    *buffer = (uint8_t *)&active_frame[(size_t)packet_index * payload_capacity];
    *size = (uint16_t)(remainder + 2U);
    ++packet_index;
  } else {
    /* Header-only packet terminates the payload. */
    *buffer = (uint8_t *)active_frame;
    *size = 2U;
    packet_index = 0U;
  }
  return 0;
}

USBD_VIDEO_ItfTypeDef g_usb_video_interface = {
    video_init,
    video_deinit,
    video_control,
    video_data,
};

