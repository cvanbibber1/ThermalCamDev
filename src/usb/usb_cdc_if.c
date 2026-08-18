#include "usb_cdc_if.h"

#include "command_dispatch.h"
#include "health.h"
#include "protocol/wire_protocol.h"

#include <string.h>

extern USBD_HandleTypeDef g_usb_device;
extern uint8_t g_usb_cdc_class_id;

#define CDC_RX_PACKET_SIZE 64U
#define CDC_RING_SIZE 512U

static uint8_t cdc_rx_packet[CDC_RX_PACKET_SIZE];
static uint8_t cdc_tx_encoded[WIRE_MAX_ENCODED_SIZE];
static uint8_t cdc_ring[CDC_RING_SIZE];
static volatile uint16_t cdc_ring_write;
static uint16_t cdc_ring_read;
static wire_stream_t cdc_stream;
static bool tx_active;

static int8_t cdc_init(void) {
  wire_stream_init(&cdc_stream);
  USBD_CDC_SetTxBuffer(&g_usb_device, cdc_tx_encoded, 0U, g_usb_cdc_class_id);
  USBD_CDC_SetRxBuffer(&g_usb_device, cdc_rx_packet);
  return 0;
}

static int8_t cdc_deinit(void) {
  return 0;
}

static int8_t cdc_control(uint8_t command, uint8_t *buffer, uint16_t length) {
  (void)command;
  (void)buffer;
  (void)length;
  return 0;
}

static int8_t cdc_receive(uint8_t *buffer, uint32_t *length) {
  for (uint32_t i = 0U; i < *length; ++i) {
    uint16_t next = (uint16_t)((cdc_ring_write + 1U) % CDC_RING_SIZE);
    if (next == cdc_ring_read) {
      health_increment(&g_health.usb_rx_overruns);
      break;
    }
    cdc_ring[cdc_ring_write] = buffer[i];
    cdc_ring_write = next;
  }
  USBD_CDC_SetRxBuffer(&g_usb_device, cdc_rx_packet);
  USBD_CDC_ReceivePacket(&g_usb_device);
  return 0;
}

static int8_t cdc_transmit_complete(uint8_t *buffer, uint32_t *length, uint8_t endpoint) {
  (void)buffer;
  (void)length;
  (void)endpoint;
  tx_active = false;
  return 0;
}

USBD_CDC_ItfTypeDef g_usb_cdc_interface = {
    cdc_init,
    cdc_deinit,
    cdc_control,
    cdc_receive,
    cdc_transmit_complete,
};

static bool transmit_response(const wire_message_t *response) {
  if (tx_active) {
    health_increment(&g_health.usb_tx_busy);
    return false;
  }
  size_t length = wire_encode(response, cdc_tx_encoded, sizeof(cdc_tx_encoded));
  if (length == 0U) {
    return false;
  }
  USBD_CDC_SetTxBuffer(&g_usb_device, cdc_tx_encoded, length, g_usb_cdc_class_id);
  tx_active = true;
  if (USBD_CDC_TransmitPacket(&g_usb_device, g_usb_cdc_class_id) != USBD_OK) {
    tx_active = false;
    health_increment(&g_health.usb_tx_busy);
    return false;
  }
  return true;
}

void usb_cdc_task(void) {
  while (cdc_ring_read != cdc_ring_write) {
    uint8_t byte = cdc_ring[cdc_ring_read];
    cdc_ring_read = (uint16_t)((cdc_ring_read + 1U) % CDC_RING_SIZE);
    wire_message_t request;
    if (wire_stream_push(&cdc_stream, byte, &request)) {
      wire_message_t response;
      if (command_dispatch(&request, 0U, &response)) {
        (void)transmit_response(&response);
      }
    }
  }
  if (cdc_stream.overflows != 0U) {
    g_health.usb_rx_overruns += cdc_stream.overflows;
    cdc_stream.overflows = 0U;
  }
}

