#pragma once

#include "stm32f4xx_hal.h"

#include <stdlib.h>
#include <string.h>

#define USBD_MAX_NUM_INTERFACES 4U
#define USBD_MAX_NUM_CONFIGURATION 1U
#define USBD_MAX_STR_DESC_SIZ 128U
#define USBD_SELF_POWERED 1U
#define USBD_DEBUG_LEVEL 0U
#define USBD_MAX_SUPPORTED_CLASS 2U
#define USBD_MAX_CLASS_ENDPOINTS 3U
#define USBD_CMPST_MAX_CONFDESC_SZ 400U

#define USE_USBD_COMPOSITE
#define USBD_COMPOSITE_USE_IAD 1U
#define USBD_CMPSIT_ACTIVATE_CDC 1U
#define USBD_CMPSIT_ACTIVATE_VIDEO 1U
#define USBD_UVC_FORMAT_UNCOMPRESSED

#define UVC_WIDTH 160U
#define UVC_HEIGHT 120U
#define UVC_CAM_FPS_FS 9U
#define UVC_BITS_PER_PIXEL 16U
/* Format 1 is Y16 because the companion application reads absolute
 * temperatures from every frame, and Windows only surfaces the first payload
 * format of a streaming interface. YUY2 is advertised as format 2 by
 * usb_video_format_install for hosts that cannot decode Y16; swap
 * UVC_UNCOMPRESSED_GUID and UVC_SECOND_FORMAT_FOURCC to prefer that instead.
 * Both are 16 bits per pixel at 160x120, so the two format and frame
 * descriptors are byte-identical apart from the GUID. */
#define UVC_GUID_Y16_FOURCC 0x20363159U
#define UVC_GUID_YUY2_FOURCC 0x32595559U
#define UVC_UNCOMPRESSED_GUID UVC_GUID_YUY2_FOURCC
#define UVC_SECOND_FORMAT_FOURCC UVC_GUID_Y16_FOURCC

/* Advertising both formats does not give hosts a choice on Windows: measured
 * 2026-08-18, when Y16 and YUY2 are both present the Windows frame server
 * exposes only YUY2, regardless of which one is format 1. So a device that
 * offers both is a YUY2 device as far as Windows is concerned, and the
 * radiometric counts the companion application needs become unreachable.
 * Offer one format at a time: leave this at 0 for the radiometric build, and
 * set it to 1 with UVC_UNCOMPRESSED_GUID set to YUY2 for a build that targets
 * the stock Windows Camera app. */
#define UVC_ADVERTISE_SECOND_FORMAT 0
#define UVC_MAX_FRAME_SIZE (UVC_WIDTH * UVC_HEIGHT * 2U)
#define UVC_PACKET_SIZE 512U
#define UVC_ISO_FS_MPS 512U

#define USBD_malloc malloc
#define USBD_free free
#define USBD_memset memset
#define USBD_memcpy memcpy
#define USBD_Delay HAL_Delay

#define USBD_UsrLog(...)
#define USBD_ErrLog(...)
#define USBD_DbgLog(...)
