#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint8_t node_address;
  uint32_t baud;
  uint32_t received_messages;
  uint32_t transmitted_messages;
  bool transmitting;
} rs485_status_t;

bool rs485_init(void);
void rs485_task(void);
void rs485_get_status(rs485_status_t *status);
bool rs485_set_address(uint8_t address);
