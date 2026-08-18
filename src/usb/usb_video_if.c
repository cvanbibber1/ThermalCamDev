#include "usb_video_if.h"

#include "app_config.h"
#include "lepton_capture.h"
#include "usbd_video.h"

#include <string.h>


static uint16_t empty_frame[APP_FRAME_PIXELS];
static const uint8_t *active_frame = (const uint8_t *)empty_frame;
static uint16_t packet_index;
/* YUY2 is generated a packet at a time. The class driver copies out of this
 * before returning, and one packet's worth avoids a second full frame in RAM. */
static uint8_t yuy2_packet[UVC_PACKET_SIZE];

/* Y16 counts to YUY2. Luma is the scene stretched to 8 bits and the chroma
 * bytes are neutral, giving hosts that cannot read Y16 a plain white-hot
 * image. YUY2 stores Y0 U Y1 V, so within the packed stream every even byte is
 * a luma sample and every odd byte is chroma. */
static const uint8_t *build_yuy2(size_t byte_offset, uint16_t length) {
  lepton_agc_t agc;
  lepton_capture_get_agc(&agc);
  const uint16_t *pixels = (const uint16_t *)(const void *)active_frame;

  for (uint16_t index = 0U; index < length; ++index) {
    size_t position = byte_offset + index;
    if ((position & 1U) != 0U) {
      yuy2_packet[index] = 128U;
      continue;
    }
    uint16_t value = pixels[position >> 1];
    uint32_t luma = 0U;
    if (value > agc.minimum) {
      luma = ((uint32_t)(value - agc.minimum) * agc.scale) >> 16;
    }
    yuy2_packet[index] = (uint8_t)(luma > 255U ? 255U : luma);
  }
  return yuy2_packet;
}

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

  /* Send raw counts for whichever index carries Y16 and convert for the other. */
  uint8_t committed = USBD_VIDEO_GetCommittedFormat();
  const bool radiometric =
      (UVC_UNCOMPRESSED_GUID == UVC_GUID_Y16_FOURCC)
          ? (committed != UVC_FORMAT_INDEX_SECOND)
          : (committed == UVC_FORMAT_INDEX_SECOND);

  *index = packet_index;
  if ((packet_index < full_packets) ||
      ((packet_index == full_packets) && (remainder != 0U))) {
    size_t offset = (size_t)packet_index * payload_capacity;
    uint16_t payload =
        packet_index < full_packets ? payload_capacity : remainder;
    *buffer = radiometric ? (uint8_t *)&active_frame[offset]
                          : (uint8_t *)build_yuy2(offset, payload);
    *size = (uint16_t)(payload + 2U);
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

