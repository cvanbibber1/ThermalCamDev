#pragma once

#include <stddef.h>
#include <stdint.h>

uint16_t crc16_ccitt(const uint8_t *data, size_t length, uint16_t seed);
uint32_t crc32c(const uint8_t *data, size_t length);

