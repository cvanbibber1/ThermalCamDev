#include "usbd_desc.h"

#include "board.h"
#include "usbd_conf.h"
#include "usbd_core.h"

#define USB_DEV_VID 0x1209U
#define USB_DEV_PID 0xF412U
#define USB_LANGID 0x0409U
/* bcdDevice. Bump whenever the configuration descriptor changes so Windows
 * re-reads it instead of serving a cached copy for this VID/PID/serial. */
#define USB_DEV_BCD 0x0303U
#define USB_SERIAL_DESCRIPTOR_SIZE 26U

static uint8_t *device_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *lang_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *manufacturer_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *product_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *serial_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *config_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *interface_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);

USBD_DescriptorsTypeDef g_usb_descriptors = {
    device_descriptor,
    lang_descriptor,
    manufacturer_descriptor,
    product_descriptor,
    serial_descriptor,
    config_descriptor,
    interface_descriptor,
};

__ALIGN_BEGIN static uint8_t device_desc[USB_LEN_DEV_DESC] __ALIGN_END = {
    USB_LEN_DEV_DESC, USB_DESC_TYPE_DEVICE,
    0x00U, 0x02U,
    0xEFU, 0x02U, 0x01U,
    USB_MAX_EP0_SIZE,
    LOBYTE(USB_DEV_VID), HIBYTE(USB_DEV_VID),
    LOBYTE(USB_DEV_PID), HIBYTE(USB_DEV_PID),
    LOBYTE(USB_DEV_BCD), HIBYTE(USB_DEV_BCD),
    USBD_IDX_MFC_STR, USBD_IDX_PRODUCT_STR, USBD_IDX_SERIAL_STR,
    USBD_MAX_NUM_CONFIGURATION,
};

__ALIGN_BEGIN static uint8_t lang_desc[USB_LEN_LANGID_STR_DESC] __ALIGN_END = {
    USB_LEN_LANGID_STR_DESC, USB_DESC_TYPE_STRING, LOBYTE(USB_LANGID), HIBYTE(USB_LANGID),
};

__ALIGN_BEGIN static uint8_t string_desc[USBD_MAX_STR_DESC_SIZ] __ALIGN_END;
__ALIGN_BEGIN static uint8_t serial_desc[USB_SERIAL_DESCRIPTOR_SIZE] __ALIGN_END = {
    USB_SERIAL_DESCRIPTOR_SIZE, USB_DESC_TYPE_STRING,
};

static void unicode_hex(uint32_t value, uint8_t *buffer, uint8_t digits) {
  for (uint8_t i = 0U; i < digits; ++i) {
    uint8_t nibble = (uint8_t)(value >> 28);
    buffer[i * 2U] = (uint8_t)(nibble < 10U ? ('0' + nibble) : ('A' + nibble - 10U));
    buffer[i * 2U + 1U] = 0U;
    value <<= 4;
  }
}

static uint8_t *device_descriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed;
  *length = sizeof(device_desc);
  return device_desc;
}

static uint8_t *lang_descriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed;
  *length = sizeof(lang_desc);
  return lang_desc;
}

static uint8_t *manufacturer_descriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed;
  USBD_GetString((uint8_t *)"ThermalCamDev", string_desc, length);
  return string_desc;
}

static uint8_t *product_descriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed;
  USBD_GetString((uint8_t *)"Lepton 3.1R Radiometric Camera", string_desc, length);
  return string_desc;
}

static uint8_t *serial_descriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed;
  uint32_t first = board_unique_id_word(0U) ^ board_unique_id_word(2U);
  uint32_t second = board_unique_id_word(1U);
  unicode_hex(first, &serial_desc[2], 8U);
  unicode_hex(second, &serial_desc[18], 4U);
  *length = sizeof(serial_desc);
  return serial_desc;
}

static uint8_t *config_descriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed;
  USBD_GetString((uint8_t *)"UVC + CDC", string_desc, length);
  return string_desc;
}

static uint8_t *interface_descriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed;
  USBD_GetString((uint8_t *)"Thermal camera control", string_desc, length);
  return string_desc;
}

