#include "protocol/cobs.h"

size_t cobs_encode(const uint8_t *input, size_t input_length,
                   uint8_t *output, size_t output_capacity) {
  if ((output == NULL) || (output_capacity == 0U) || ((input == NULL) && (input_length != 0U))) {
    return 0U;
  }

  size_t read_index = 0U;
  size_t write_index = 1U;
  size_t code_index = 0U;
  uint8_t code = 1U;

  while (read_index < input_length) {
    if (input[read_index] == 0U) {
      if (code_index >= output_capacity) {
        return 0U;
      }
      output[code_index] = code;
      code_index = write_index++;
      code = 1U;
      ++read_index;
    } else {
      if (write_index >= output_capacity) {
        return 0U;
      }
      output[write_index++] = input[read_index++];
      ++code;
      if (code == 0xFFU) {
        if (code_index >= output_capacity) {
          return 0U;
        }
        output[code_index] = code;
        code_index = write_index++;
        code = 1U;
      }
    }
  }

  if (code_index >= output_capacity) {
    return 0U;
  }
  output[code_index] = code;
  return write_index;
}

size_t cobs_decode(const uint8_t *input, size_t input_length,
                   uint8_t *output, size_t output_capacity) {
  if ((input == NULL) || (output == NULL) || (input_length == 0U)) {
    return 0U;
  }

  size_t read_index = 0U;
  size_t write_index = 0U;
  while (read_index < input_length) {
    uint8_t code = input[read_index++];
    if (code == 0U) {
      return 0U;
    }
    size_t copy = (size_t)code - 1U;
    if ((read_index + copy > input_length) || (write_index + copy > output_capacity)) {
      return 0U;
    }
    for (size_t i = 0U; i < copy; ++i) {
      output[write_index++] = input[read_index++];
    }
    if ((code != 0xFFU) && (read_index < input_length)) {
      if (write_index >= output_capacity) {
        return 0U;
      }
      output[write_index++] = 0U;
    }
  }
  return write_index;
}

