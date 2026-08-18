#include "protocol/crc.h"

/* Nibble-wise table for polynomial 0x1021. VoSPI checks every byte of every
 * packet, so the bit-serial loop dominated the capture task's budget; this
 * costs 32 bytes of flash and produces identical results. */
static const uint16_t crc16_ccitt_table[16] = {
    0x0000U, 0x1021U, 0x2042U, 0x3063U, 0x4084U, 0x50A5U, 0x60C6U, 0x70E7U,
    0x8108U, 0x9129U, 0xA14AU, 0xB16BU, 0xC18CU, 0xD1ADU, 0xE1CEU, 0xF1EFU,
};

uint16_t crc16_ccitt(const uint8_t *data, size_t length, uint16_t seed) {
  uint16_t crc = seed;
  for (size_t i = 0; i < length; ++i) {
    crc = (uint16_t)((crc << 4) ^
                     crc16_ccitt_table[((crc >> 12) ^ (data[i] >> 4)) & 0x0FU]);
    crc = (uint16_t)((crc << 4) ^
                     crc16_ccitt_table[((crc >> 12) ^ (data[i] & 0x0FU)) & 0x0FU]);
  }
  return crc;
}

uint32_t crc32c(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
      crc = (crc >> 1) ^ (0x82F63B78U & mask);
    }
  }
  return ~crc;
}

