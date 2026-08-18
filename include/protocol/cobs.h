#pragma once

#include <stddef.h>
#include <stdint.h>

size_t cobs_encode(const uint8_t *input, size_t input_length,
                   uint8_t *output, size_t output_capacity);
size_t cobs_decode(const uint8_t *input, size_t input_length,
                   uint8_t *output, size_t output_capacity);

