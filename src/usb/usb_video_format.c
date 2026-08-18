/* Advertise a second UVC payload format.
 *
 * CubeF4's composite builder emits exactly one payload format, and its
 * descriptor generator is static inside usbd_composite_builder.c, so it cannot
 * be extended in place. Rather than fork that file, this rewrites the
 * configuration descriptor it produced and takes over the accessor the core
 * calls to hand it to the host. USBD_CMPSIT is not const, so replacing its
 * descriptor callback is enough.
 *
 * Both formats are 160x120 at 16 bits per pixel, so the format, frame, and
 * colour-matching descriptors for Y16 are byte-identical to the YUY2 ones
 * except for the format index and the GUID. The second block is therefore a
 * copy with two edits rather than a hand-built descriptor.
 */

#include "usbd_video.h"

#include "usbd_composite_builder.h"
#include "usbd_def.h"

#include <string.h>

#define CS_INTERFACE_TYPE 0x24U
#define VS_INPUT_HEADER_SUBTYPE 0x01U
#define VS_FORMAT_UNCOMPRESSED_SUBTYPE 0x04U
#define VS_FORMAT_DESC_LEN 27U
#define VS_FRAME_DESC_LEN 30U
#define VS_COLOR_DESC_LEN 6U
#define VS_FORMAT_BLOCK_LEN (VS_FORMAT_DESC_LEN + VS_FRAME_DESC_LEN + VS_COLOR_DESC_LEN)

/* Offsets inside a payload format descriptor. */
#define FORMAT_INDEX_OFFSET 3U
#define FORMAT_GUID_OFFSET 5U

/* Offsets inside the class-specific VS input header. */
#define VS_HEADER_NUM_FORMATS_OFFSET 3U
#define VS_HEADER_TOTAL_LENGTH_OFFSET 4U

/* Word aligned: the OTG driver copies descriptors into the FIFO 32 bits
 * at a time. */
_Alignas(4) static uint8_t video_config_descriptor[USBD_CMPST_MAX_CONFDESC_SZ];
static uint16_t video_config_length;

static uint8_t *video_get_config_descriptor(uint16_t *length) {
  *length = video_config_length;
  return video_config_descriptor;
}

void usb_video_format_install(USBD_HandleTypeDef *pdev) {
  (void)pdev;
  uint16_t length = 0U;
  uint8_t *source = USBD_CMPSIT.GetFSConfigDescriptor(&length);
  if ((source == NULL) || (length == 0U) ||
      ((size_t)length + VS_FORMAT_BLOCK_LEN > sizeof(video_config_descriptor))) {
    return;
  }

  /* Walk the descriptor chain to the streaming header and the format block it
   * introduces. Both are located by descriptor type rather than by a fixed
   * offset so this survives changes to the classes registered before video. */
  size_t header = 0U;
  size_t format = 0U;
  for (size_t index = 0U; (index + 2U) < length;) {
    uint8_t item_length = source[index];
    if (item_length < 2U) {
      return;
    }
    if (source[index + 1U] == CS_INTERFACE_TYPE) {
      if ((source[index + 2U] == VS_INPUT_HEADER_SUBTYPE) && (format == 0U)) {
        /* Subtype 1 is also the video control header and, further along, a CDC
         * functional descriptor, so keep the last one seen before the payload
         * format and stop looking once that format is found. */
        header = index;
      } else if ((source[index + 2U] == VS_FORMAT_UNCOMPRESSED_SUBTYPE) &&
                 (header != 0U) && (format == 0U)) {
        format = index;
        break;
      }
    }
    index += item_length;
  }
  if ((header == 0U) || (format == 0U) ||
      ((format + VS_FORMAT_BLOCK_LEN) > length)) {
    return;
  }

  /* Two insertions are needed. The class-specific VS input header carries one
   * bmaControls byte per format, so it grows by one byte, and the payload
   * format block is duplicated for Y16. */
  size_t header_end = header + (size_t)source[header];
  size_t split = format + VS_FORMAT_BLOCK_LEN;
  if ((header_end > format) ||
      ((size_t)length + VS_FORMAT_BLOCK_LEN + 1U > sizeof(video_config_descriptor))) {
    return;
  }

  size_t out = 0U;
  (void)memcpy(&video_config_descriptor[out], source, header_end);
  out += header_end;
  /* bmaControls for format 2: no controls, same as format 1. */
  video_config_descriptor[out] = 0U;
  out += 1U;
  (void)memcpy(&video_config_descriptor[out], &source[header_end],
               split - header_end);
  out += split - header_end;
  size_t copy_at = out;
  (void)memcpy(&video_config_descriptor[out], &source[format],
               VS_FORMAT_BLOCK_LEN);
  out += VS_FORMAT_BLOCK_LEN;
  (void)memcpy(&video_config_descriptor[out], &source[split],
               (size_t)length - split);
  out += (size_t)length - split;
  video_config_length = (uint16_t)out;

  uint8_t *copy = &video_config_descriptor[copy_at];
  copy[FORMAT_INDEX_OFFSET] = UVC_FORMAT_INDEX_Y16;
  /* Only the first four GUID bytes differ; the rest is the standard
   * {xxxxxxxx-0000-0010-8000-00AA00389B71} uncompressed suffix. */
  copy[FORMAT_GUID_OFFSET + 0U] = (uint8_t)(UVC_GUID_Y16_FOURCC);
  copy[FORMAT_GUID_OFFSET + 1U] = (uint8_t)(UVC_GUID_Y16_FOURCC >> 8);
  copy[FORMAT_GUID_OFFSET + 2U] = (uint8_t)(UVC_GUID_Y16_FOURCC >> 16);
  copy[FORMAT_GUID_OFFSET + 3U] = (uint8_t)(UVC_GUID_Y16_FOURCC >> 24);

  uint8_t *vs_header = &video_config_descriptor[header];
  vs_header[0] = (uint8_t)(vs_header[0] + 1U);
  vs_header[VS_HEADER_NUM_FORMATS_OFFSET] = 2U;
  uint16_t vs_total =
      (uint16_t)(vs_header[VS_HEADER_TOTAL_LENGTH_OFFSET] |
                 ((uint16_t)vs_header[VS_HEADER_TOTAL_LENGTH_OFFSET + 1U] << 8));
  vs_total = (uint16_t)(vs_total + VS_FORMAT_BLOCK_LEN + 1U);
  vs_header[VS_HEADER_TOTAL_LENGTH_OFFSET] = LOBYTE(vs_total);
  vs_header[VS_HEADER_TOTAL_LENGTH_OFFSET + 1U] = HIBYTE(vs_total);

  /* wTotalLength of the configuration descriptor itself. */
  video_config_descriptor[2] = LOBYTE(video_config_length);
  video_config_descriptor[3] = HIBYTE(video_config_length);

  USBD_CMPSIT.GetFSConfigDescriptor = video_get_config_descriptor;
  USBD_CMPSIT.GetOtherSpeedConfigDescriptor = video_get_config_descriptor;
}
