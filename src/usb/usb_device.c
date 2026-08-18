#include "usb_device.h"

#include "usb_cdc_if.h"
#include "usb_video_if.h"
#include "usbd_cdc.h"
#include "usbd_composite_builder.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_video.h"

USBD_HandleTypeDef g_usb_device;
uint8_t g_usb_cdc_class_id = 0xFFU;
uint8_t g_usb_video_class_id = 0xFFU;

bool usb_device_init(void) {
  static uint8_t cdc_endpoints[] = {0x82U, 0x02U, 0x83U};
  static uint8_t video_endpoints[] = {0x81U};

  if (USBD_Init(&g_usb_device, &g_usb_descriptors, 0U) != USBD_OK) {
    return false;
  }

  /* CubeF4's composite UVC descriptor contains fixed references to video
   * interfaces 0 and 1, so VIDEO must be the first registered class. */
  if (USBD_RegisterClassComposite(&g_usb_device, USBD_VIDEO_CLASS,
                                  CLASS_TYPE_VIDEO, video_endpoints) != USBD_OK) {
    return false;
  }
  g_usb_video_class_id = (uint8_t)USBD_CMPSIT_SetClassID(&g_usb_device,
                                                         CLASS_TYPE_VIDEO, 0U);
  if ((g_usb_video_class_id == 0xFFU) ||
      (USBD_VIDEO_RegisterInterface(&g_usb_device, &g_usb_video_interface) != USBD_OK)) {
    return false;
  }

  g_usb_device.classId = g_usb_device.NumClasses;
  if (USBD_RegisterClassComposite(&g_usb_device, USBD_CDC_CLASS,
                                  CLASS_TYPE_CDC, cdc_endpoints) != USBD_OK) {
    return false;
  }
  g_usb_cdc_class_id = (uint8_t)USBD_CMPSIT_SetClassID(&g_usb_device,
                                                       CLASS_TYPE_CDC, 0U);
  if ((g_usb_cdc_class_id == 0xFFU) ||
      (USBD_CDC_RegisterInterface(&g_usb_device, &g_usb_cdc_interface) != USBD_OK)) {
    return false;
  }
  /* Must run after both classes are built into the descriptor and before the
   * device is started, so the host only ever sees the two-format version. */
  usb_video_format_install(&g_usb_device);

  return USBD_Start(&g_usb_device) == USBD_OK;
}

void usb_device_task(void) {
  usb_cdc_task();
}

bool usb_device_configured(void) {
  return g_usb_device.dev_state == USBD_STATE_CONFIGURED;
}
