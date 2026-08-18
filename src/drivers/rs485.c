#include "rs485.h"

#include "app_config.h"
#include "board.h"
#include "command_dispatch.h"
#include "health.h"
#include "protocol/wire_protocol.h"

#include <string.h>

#define RS485_RX_DMA_SIZE 512U
#define RS485_MASTER_ADDRESS 0xF0U
#define RS485_BROADCAST_ADDRESS 0xFFU

static uint8_t rx_dma[RS485_RX_DMA_SIZE];
static uint16_t rx_position;
static uint8_t tx_encoded[WIRE_MAX_ENCODED_SIZE];
static wire_stream_t rx_stream;
static rs485_status_t current;
static wire_message_t scheduled_response;
static bool response_scheduled;
static uint32_t response_due_ms;

static bool begin_transmit(const wire_message_t *message) {
  if (current.transmitting) {
    health_increment(&g_health.rs485_tx_busy);
    return false;
  }
  size_t length = wire_encode(message, tx_encoded, sizeof(tx_encoded));
  if (length == 0U) {
    return false;
  }
  board_rs485_de(true);
  current.transmitting = true;
  if (HAL_UART_Transmit_DMA(&huart2, tx_encoded, (uint16_t)length) != HAL_OK) {
    current.transmitting = false;
    board_rs485_de(false);
    return false;
  }
  return true;
}

bool rs485_init(void) {
  memset(&current, 0, sizeof(current));
  current.node_address = APP_NODE_ADDRESS_DEFAULT;
  current.baud = APP_RS485_BAUD;
  wire_stream_init(&rx_stream);
  board_rs485_de(false);
  return HAL_UART_Receive_DMA(&huart2, rx_dma, sizeof(rx_dma)) == HAL_OK;
}

static void handle_message(const wire_message_t *request) {
  if ((request->destination != current.node_address) &&
      (request->destination != RS485_BROADCAST_ADDRESS)) {
    return;
  }
  if ((request->destination == RS485_BROADCAST_ADDRESS) &&
      (request->opcode != OPCODE_DISCOVER) &&
      (request->opcode != OPCODE_BUS_ASSIGN_ADDRESS)) {
    return;
  }

  wire_message_t response;
  if (!command_dispatch(request, current.node_address, &response)) {
    return;
  }
  ++current.received_messages;
  if (request->destination == RS485_BROADCAST_ADDRESS) {
    scheduled_response = response;
    uint32_t hash = board_unique_id_word(0U) ^ board_unique_id_word(1U) ^
                    board_unique_id_word(2U);
    response_due_ms = HAL_GetTick() + 1U + (hash % 31U);
    response_scheduled = true;
  } else if (begin_transmit(&response)) {
    ++current.transmitted_messages;
  }
}

void rs485_task(void) {
  uint16_t write_position = (uint16_t)(RS485_RX_DMA_SIZE -
      __HAL_DMA_GET_COUNTER(huart2.hdmarx));
  while (rx_position != write_position) {
    wire_message_t message;
    uint8_t byte = rx_dma[rx_position++];
    if (rx_position == RS485_RX_DMA_SIZE) {
      rx_position = 0U;
    }
    if (wire_stream_push(&rx_stream, byte, &message)) {
      handle_message(&message);
    }
  }
  if (rx_stream.overflows != 0U) {
    g_health.rs485_rx_overruns += rx_stream.overflows;
    rx_stream.overflows = 0U;
  }
  if (rx_stream.decode_errors != 0U) {
    g_health.rs485_crc_errors += rx_stream.decode_errors;
    rx_stream.decode_errors = 0U;
  }
  if (response_scheduled && !current.transmitting &&
      ((int32_t)(HAL_GetTick() - response_due_ms) >= 0)) {
    response_scheduled = false;
    if (begin_transmit(&scheduled_response)) {
      ++current.transmitted_messages;
    }
  }
}

void rs485_get_status(rs485_status_t *status) {
  if (status != NULL) {
    __disable_irq();
    *status = current;
    __enable_irq();
  }
}

bool rs485_set_address(uint8_t address) {
  if ((address == 0U) || (address >= RS485_MASTER_ADDRESS)) {
    return false;
  }
  current.node_address = address;
  return true;
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *handle) {
  if (handle->Instance == USART2) {
    board_rs485_de(false);
    current.transmitting = false;
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *handle) {
  if (handle->Instance == USART2) {
    board_rs485_de(false);
    current.transmitting = false;
    health_increment(&g_health.rs485_rx_overruns);
    (void)HAL_UART_Receive_DMA(&huart2, rx_dma, sizeof(rx_dma));
    rx_position = 0U;
  }
}
