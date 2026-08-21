#include "protocol/frame_codec.h"

#include <string.h>

/* The project builds at -Os, which leaves this file about five times slower
 * than it needs to be: none of the per-pixel helpers get inlined, and the call
 * overhead alone pushed a frame to 56 ms. The encoder has a hard deadline --
 * the VoSPI DMA must be serviced every 12.6 ms or the sensor drops into resync
 * -- so this one file is built for speed. Nothing else changes. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O3")
#endif

#define PIXELS APP_FRAME_PIXELS
#define WIDTH APP_FRAME_WIDTH

/* ------------------------------------------------------------ bit stream -- */

/* Declared in the header so an encoder in progress can carry one. */
typedef frame_codec_bits_t bitstream_t;

static void bits_init_write(bitstream_t *stream, uint8_t *buffer,
                            size_t capacity) {
  stream->buffer = buffer;
  stream->source = buffer;
  stream->capacity = capacity;
  stream->position = 0U;
  stream->cursor = 0U;
  stream->accumulator = 0U;
  stream->pending = 0U;
  stream->overflow = false;
}

static void bits_init_read(bitstream_t *stream, const uint8_t *source,
                           size_t capacity) {
  stream->buffer = NULL;
  stream->source = source;
  stream->capacity = capacity;
  stream->position = 0U;
  stream->cursor = 0U;
  stream->accumulator = 0U;
  stream->pending = 0U;
  stream->overflow = false;
}

/* Bits accumulate in a register and leave a byte at a time.
 *
 * Writing them one at a time costs about 30 ms a frame on this part, which is
 * more than the 12.6 ms the VoSPI DMA can be left unserviced: the sensor loses
 * chunks and drops into resync. `count` is at most 24, so the accumulator
 * cannot overflow before it is drained. */
static inline void bits_put(bitstream_t *stream, uint32_t value, unsigned count) {
  if ((count == 0U) || (count > 24U)) {
    return;
  }
  uint32_t mask = (1UL << count) - 1UL;
  stream->accumulator = (stream->accumulator << count) | (value & mask);
  stream->pending += count;
  stream->position += count;
  while (stream->pending >= 8U) {
    if (stream->cursor >= stream->capacity) {
      stream->overflow = true;
      stream->pending = 0U;
      return;
    }
    stream->pending -= 8U;
    stream->buffer[stream->cursor] =
        (uint8_t)(stream->accumulator >> stream->pending);
    ++stream->cursor;
  }
}

/* Push the last partial byte out, zero padded. */
static void bits_flush(bitstream_t *stream) {
  if (stream->pending == 0U) {
    return;
  }
  unsigned padding = 8U - stream->pending;
  bits_put(stream, 0U, padding);
  stream->position -= padding;
}

static uint32_t bits_get(bitstream_t *stream, unsigned count) {
  uint32_t value = 0U;
  for (unsigned index = 0U; index < count; ++index) {
    size_t bit = stream->position;
    size_t byte = bit >> 3;
    if (byte >= stream->capacity) {
      stream->overflow = true;
      return 0U;
    }
    uint8_t mask = (uint8_t)(0x80U >> (bit & 7U));
    value = (value << 1) | ((stream->source[byte] & mask) != 0U ? 1U : 0U);
    ++stream->position;
  }
  return value;
}

/* --------------------------------------------------------------- coding -- */

/* Fold a signed residual onto the naturals so small magnitudes of either sign
 * stay small: 0, -1, 1, -2, 2 becomes 0, 1, 2, 3, 4. */
static inline uint32_t zigzag(int32_t value) {
  return ((uint32_t)value << 1) ^ (uint32_t)(value >> 31);
}

static int32_t unzigzag(uint32_t value) {
  return (int32_t)(value >> 1) ^ -(int32_t)(value & 1U);
}

/* The smallest k whose bucket covers the block mean. This is the classic Rice
 * estimator and it costs one pass over the block.
 *
 * Costing the neighbouring k values as well matches an exhaustive search
 * exactly, but it triples the work in the hottest loop in the firmware and buys
 * 3.4% -- measured over 50 real frames. At 8.7 frames a second against a
 * 12.6 ms deadline, the time is worth more than the bytes. */
static unsigned choose_k(const uint32_t *values, unsigned count) {
  uint32_t sum = 0U;
  for (unsigned index = 0U; index < count; ++index) {
    sum += values[index];
  }
  uint32_t mean = sum / count;
  unsigned k = 0U;
  while ((k < 15U) && ((1UL << k) < mean)) {
    ++k;
  }
  return k;
}

static inline void rice_put(bitstream_t *stream, uint32_t value, unsigned k) {
  uint32_t quotient = value >> k;
  if (quotient >= FRAME_CODEC_ESCAPE) {
    bits_put(stream, 0xFFFFFFFFU, FRAME_CODEC_ESCAPE);
    bits_put(stream, value, 24U);
    return;
  }
  /* `quotient` ones then a terminating zero, as one write. The quotient is
   * below the escape threshold here, so this is at most 20 bits. */
  bits_put(stream, ((1UL << quotient) - 1UL) << 1, quotient + 1U);
  if (k > 0U) {
    bits_put(stream, value, k);
  }
}

static uint32_t rice_get(bitstream_t *stream, unsigned k) {
  uint32_t quotient = 0U;
  while (quotient < FRAME_CODEC_ESCAPE) {
    if (bits_get(stream, 1U) == 0U) {
      break;
    }
    ++quotient;
    if (stream->overflow) {
      return 0U;
    }
  }
  if (quotient >= FRAME_CODEC_ESCAPE) {
    return bits_get(stream, 24U);
  }
  uint32_t remainder = (k > 0U) ? bits_get(stream, k) : 0U;
  return (quotient << k) | remainder;
}

/* ------------------------------------------------------------ predictor -- */

/* Median edge detection, as used by JPEG-LS. Given the pixel to the left (a),
 * the one above (b) and the one above-left (c), it picks b at a vertical edge,
 * a at a horizontal one, and the planar estimate elsewhere. Three comparisons
 * and an add, which is what makes it affordable here. */
static inline int32_t predict(int32_t a, int32_t b, int32_t c) {
  int32_t high = a > b ? a : b;
  int32_t low = a < b ? a : b;
  if (c >= high) {
    return low;
  }
  if (c <= low) {
    return high;
  }
  return a + b - c;
}

static inline int32_t neighbours(const int32_t *row_above, const int32_t *row, size_t x,
                          size_t y) {
  int32_t a = (x > 0U) ? row[x - 1U] : ((y > 0U) ? row_above[0] : 0);
  int32_t b = (y > 0U) ? row_above[x] : a;
  int32_t c = ((x > 0U) && (y > 0U)) ? row_above[x - 1U] : b;
  return predict(a, b, c);
}

/* FNV-1a, folded in one pixel at a time so the frame can be hashed by the
 * same pass that encodes it. Cheap, and strong enough to catch a
 * reconstruction that has gone wrong. */
#define CHECKSUM_SEED 2166136261U

static inline uint32_t checksum_step(uint32_t hash, uint16_t pixel) {
  hash = (hash ^ (uint32_t)(pixel & 0xFFU)) * 16777619U;
  return (hash ^ (uint32_t)(pixel >> 8)) * 16777619U;
}

uint32_t frame_codec_checksum(const uint16_t *frame) {
  uint32_t hash = CHECKSUM_SEED;
  for (size_t index = 0U; index < PIXELS; ++index) {
    hash = checksum_step(hash, frame[index]);
  }
  return hash;
}

/* ------------------------------------------------------------- encoding -- */

/* Encoding is resumable, a band of rows at a time.
 *
 * A whole frame costs about 16 ms on this part, and the VoSPI DMA has to be
 * serviced every 12.6 ms or the sensor loses chunks and drops into resync.
 * Doing the work in one call cost roughly a third of the frame rate. Splitting
 * it lets the capture task run in between, at no extra total cost.
 *
 * Within a band the pass is strictly top to bottom and reads each pixel once.
 * The caller is expected to hold the published frame for the duration, because
 * across several calls the parser would otherwise have time to reclaim the
 * buffer and overwrite it. The reference copy is taken from the same read as
 * the encode, so what is left behind is exactly what was transmitted even if
 * the source does change underneath. */

static void encode_block(frame_codec_encoder_t *encoder) {
  unsigned k = choose_k(encoder->block_values, encoder->filled);
  bits_put(&encoder->stream, k, 4U);
  for (unsigned i = 0U; i < encoder->filled; ++i) {
    rice_put(&encoder->stream, encoder->block_values[i], k);
  }
  encoder->filled = 0U;
}

void frame_codec_encode_begin(frame_codec_encoder_t *encoder,
                              const uint16_t *frame, const uint16_t *reference,
                              uint8_t *out, size_t capacity,
                              uint16_t *capture) {
  encoder->frame = frame;
  encoder->reference = reference;
  encoder->capture = capture;
  encoder->checksum = CHECKSUM_SEED;
  encoder->filled = 0U;
  encoder->y = 0U;
  bits_init_write(&encoder->stream, out, capacity);
}

frame_codec_step_t frame_codec_encode_step(frame_codec_encoder_t *encoder,
                                           unsigned rows, size_t *length) {
  const uint16_t *frame = encoder->frame;
  const uint16_t *reference = encoder->reference;
  uint16_t *capture = encoder->capture;

  unsigned limit = encoder->y + rows;
  if (limit > APP_FRAME_HEIGHT) {
    limit = APP_FRAME_HEIGHT;
  }

  for (unsigned y = encoder->y; y < limit; ++y) {
    int32_t *row = encoder->rows[y & 1U];
    const int32_t *above = encoder->rows[(y + 1U) & 1U];
    size_t index = (size_t)y * WIDTH;
    for (unsigned x = 0U; x < WIDTH; ++x, ++index) {
      uint16_t pixel = frame[index];
      encoder->checksum = checksum_step(encoder->checksum, pixel);
      int32_t value = (int32_t)pixel;
      if (reference != NULL) {
        value -= (int32_t)reference[index];
      }
      /* Read before write, so the old reference is consumed above and the new
       * one is laid down here even when both are the same buffer. */
      if (capture != NULL) {
        capture[index] = pixel;
      }
      row[x] = value;
      int32_t residual = ((x == 0U) && (y == 0U))
                             ? value
                             : (value - neighbours(above, row, x, y));
      encoder->block_values[encoder->filled] = zigzag(residual);
      ++encoder->filled;
      if (encoder->filled == FRAME_CODEC_BLOCK) {
        encode_block(encoder);
      }
    }
    if (encoder->stream.overflow) {
      return FRAME_CODEC_OVERFLOW;
    }
  }

  encoder->y = (uint16_t)limit;
  if (limit < APP_FRAME_HEIGHT) {
    return FRAME_CODEC_BUSY;
  }

  if (encoder->filled > 0U) {
    encode_block(encoder);
  }
  bits_flush(&encoder->stream);
  /* The checksum goes at the end, not the front. It is only known once the
   * pass has finished, and a field at the front could not be filled in after
   * the chunk containing it had already been transmitted -- which is exactly
   * what the link does while the encode is still running. */
  bits_put(&encoder->stream, encoder->checksum >> 16, 16U);
  bits_put(&encoder->stream, encoder->checksum & 0xFFFFU, 16U);
  if (encoder->stream.overflow) {
    return FRAME_CODEC_OVERFLOW;
  }
  *length = encoder->stream.cursor;
  return FRAME_CODEC_DONE;
}

size_t frame_codec_encode_available(const frame_codec_encoder_t *encoder) {
  return encoder->stream.cursor;
}

/* The one-shot form, for the host tools and the tests. On target the stepped
 * form is used instead, so that a frame never blocks the capture task. */
static frame_codec_encoder_t one_shot;

size_t frame_codec_encode(const uint16_t *frame, const uint16_t *reference,
                          uint8_t *out, size_t capacity, uint8_t *mode,
                          uint16_t *capture) {
  if ((frame == NULL) || (out == NULL) || (mode == NULL)) {
    return 0U;
  }

  /* Inter is tried first and kept unless it overflows, because on this camera
   * it wins on all but the frame after a correction. */
  if (reference != NULL) {
    size_t length = 0U;
    frame_codec_encode_begin(&one_shot, frame, reference, out, capacity, capture);
    if (frame_codec_encode_step(&one_shot, APP_FRAME_HEIGHT, &length) ==
        FRAME_CODEC_DONE) {
      *mode = FRAME_CODEC_MODE_INTER;
      return length;
    }
    /* The reference was partly overwritten before the overflow, so it is no
     * longer the previous frame. The intra pass below does not read it and
     * rewrites it in full. */
  }

  size_t length = 0U;
  frame_codec_encode_begin(&one_shot, frame, NULL, out, capacity, capture);
  if (frame_codec_encode_step(&one_shot, APP_FRAME_HEIGHT, &length) ==
      FRAME_CODEC_DONE) {
    *mode = FRAME_CODEC_MODE_INTRA;
    return length;
  }

  /* Neither predictor fitted the buffer. The caller sends the frame as it
   * stands, straight from the capture buffer, so no space is reserved here
   * for an outcome that costs more RAM than the compressed path ever needs. */
  *mode = FRAME_CODEC_MODE_RAW;
  return 0U;
}

/* ------------------------------------------------------------- decoding -- */

bool frame_codec_decode(const uint8_t *in, size_t length, uint8_t mode,
                        const uint16_t *reference, uint16_t *frame) {
  if ((in == NULL) || (frame == NULL)) {
    return false;
  }
  if ((mode == FRAME_CODEC_MODE_INTER) && (reference == NULL)) {
    return false;
  }

  if (mode == FRAME_CODEC_MODE_RAW) {
    /* Uncompressed frames carry no preamble: the packet CRC already covers
     * them and there is nothing for a checksum to catch that it would not. */
    if (length < (size_t)APP_FRAME_BYTES) {
      return false;
    }
    memcpy(frame, in, APP_FRAME_BYTES);
    return true;
  }

  uint32_t expected;

  if ((mode != FRAME_CODEC_MODE_INTRA) && (mode != FRAME_CODEC_MODE_INTER)) {
    return false;
  }

  /* The decoder keeps its own two rows; the encoder's live inside its state,
   * so the two can never tread on each other. */
  static int32_t row_buffer[2][WIDTH];

  if (length < FRAME_CODEC_PREAMBLE) {
    return false;
  }
  /* The checksum is the last four bytes; everything before it is pixel data. */
  size_t body = length - FRAME_CODEC_PREAMBLE;
  expected = ((uint32_t)in[body] << 24) | ((uint32_t)in[body + 1U] << 16) |
             ((uint32_t)in[body + 2U] << 8) | in[body + 3U];

  bitstream_t stream;
  bits_init_read(&stream, in, body);

  const uint16_t *use_reference =
      (mode == FRAME_CODEC_MODE_INTER) ? reference : NULL;

  unsigned remaining = 0U;
  unsigned k = 0U;
  for (size_t y = 0U; y < APP_FRAME_HEIGHT; ++y) {
    int32_t *row = row_buffer[y & 1U];
    const int32_t *above = row_buffer[(y + 1U) & 1U];
    for (size_t x = 0U; x < WIDTH; ++x) {
      if (remaining == 0U) {
        k = (unsigned)bits_get(&stream, 4U);
        remaining = FRAME_CODEC_BLOCK;
      }
      uint32_t coded = rice_get(&stream, k);
      --remaining;
      if (stream.overflow) {
        return false;
      }
      int32_t residual = unzigzag(coded);
      size_t index = (y * WIDTH) + x;
      int32_t value = ((x == 0U) && (y == 0U))
                          ? residual
                          : (residual + neighbours(above, row, x, y));
      row[x] = value;
      if (use_reference != NULL) {
        value += (int32_t)use_reference[index];
      }
      frame[index] = (uint16_t)value;
    }
  }
  return frame_codec_checksum(frame) == expected;
}
