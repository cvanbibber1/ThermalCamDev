#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_config.h"

/* Lossless compression for Lepton frames on the RS-422 link.
 *
 * The link carries 92,160 bytes a second and a raw frame is 38,400, so
 * uncompressed video tops out near 2.3 frames a second while the sensor
 * produces 8.7. Compression is what closes that gap; there is no faster baud
 * available on this bus.
 *
 * Two predictors, chosen per frame:
 *
 *   INTRA  the median-edge predictor from JPEG-LS, run over the frame itself.
 *          Self-contained, so a decoder can start here after a dropped packet.
 *   INTER  the same predictor run over the difference against the previous
 *          frame. Much cheaper on a static scene, which is the normal case,
 *          but it is only decodable if the previous frame arrived intact.
 *
 * Residuals are Rice coded with one k per block of FRAME_CODEC_BLOCK pixels.
 * Measured over 50 consecutive frames of real handheld footage: intra averages
 * 16.3 kB and inter 9.7 kB against 37.5 kB raw.
 *
 * RAW is the escape hatch. If a frame codes larger than `capacity`, the encoder
 * declines and the caller sends the original bytes unchanged, so the link
 * degrades to the uncompressed rate rather than failing. Nothing is copied for
 * that case, which is why the encode buffer only has to fit compressed output.
 */

#define FRAME_CODEC_MODE_RAW 0U
#define FRAME_CODEC_MODE_INTRA 1U
#define FRAME_CODEC_MODE_INTER 2U

/* Pixels sharing one Rice parameter. 32 was measured against 16, 64 and a
 * single k for the whole frame; 16 gains nothing over 32 once the extra k
 * values are paid for, and 64 costs about 3%. */
#define FRAME_CODEC_BLOCK 32U

/* Unary prefixes longer than this switch to a fixed 24-bit escape, so a
 * pathological residual cannot inflate the stream without bound. */
#define FRAME_CODEC_ESCAPE 20U

/* Every encoded stream opens with a checksum of the source frame. The ground
 * side recomputes it after reconstructing and can therefore prove losslessness
 * on real traffic rather than only in a test. */
#define FRAME_CODEC_PREAMBLE 4U

/* ------------------------------------------------------- stepped encoder -- */

/* Encoding a whole frame takes about 16 ms, and the VoSPI DMA cannot be left
 * unserviced for more than 12.6 ms. The encoder therefore runs a band of rows
 * at a time, with the capture task free to run in between.
 *
 * The caller must hold the published frame across the whole sequence. Within a
 * single call the pass cannot be overtaken, but across several it could be. */
typedef enum {
  FRAME_CODEC_BUSY = 0,  /* more rows to do */
  FRAME_CODEC_DONE,      /* finished; *length is the encoded size */
  FRAME_CODEC_OVERFLOW,  /* did not fit; retry as intra, or send raw */
} frame_codec_step_t;

/* Bit stream state. Internal to the codec; it appears here only so the caller
 * can own the encoder's storage rather than the codec keeping a single global
 * encode in flight. Bits are written most significant first. */
typedef struct {
  uint8_t *buffer;       /* NULL while reading */
  const uint8_t *source; /* NULL while writing */
  size_t capacity;
  size_t position; /* bits written or read */
  size_t cursor;   /* next byte to write */
  uint32_t accumulator;
  unsigned pending; /* bits held in the accumulator, always under 8 */
  bool overflow;
} frame_codec_bits_t;

typedef struct {
  const uint16_t *frame;
  const uint16_t *reference;
  uint16_t *capture;
  frame_codec_bits_t stream;
  uint32_t checksum;
  int32_t rows[2][APP_FRAME_WIDTH];
  uint32_t block_values[FRAME_CODEC_BLOCK];
  unsigned filled;
  uint16_t y;
} frame_codec_encoder_t;

/* Start encoding `frame` into `out`. Arguments carry the same meaning as
 * frame_codec_encode() below. */
void frame_codec_encode_begin(frame_codec_encoder_t *encoder,
                              const uint16_t *frame, const uint16_t *reference,
                              uint8_t *out, size_t capacity, uint16_t *capture);

/* Encode up to `rows` more rows. On FRAME_CODEC_DONE, *length holds the
 * encoded size and the mode is whichever the caller asked for by passing or
 * withholding a reference. */
frame_codec_step_t frame_codec_encode_step(frame_codec_encoder_t *encoder,
                                           unsigned rows, size_t *length);

/* Bytes of the stream that are complete and safe to read while the encode is
 * still running. Transmitting these as they appear lets a slow link overlap
 * with the encode instead of waiting for it. The first four bytes are the
 * checksum, which is only written at the end, so they must not be sent until
 * frame_codec_encode_step() has returned FRAME_CODEC_DONE. */
size_t frame_codec_encode_available(const frame_codec_encoder_t *encoder);

/* Encode `frame` into `out`, using `reference` as the previous frame.
 *
 * Pass reference == NULL to force an intra frame; the caller must do this
 * whenever the reference is not exactly the frame that preceded this one,
 * because the decoder reconstructs from what it last received.
 *
 * `capture`, if given, receives a copy of the frame taken during the same pass
 * that encodes it, so it is guaranteed to match what was transmitted even if
 * the source buffer is being rewritten underneath. Pass the reference buffer
 * itself to roll it forward in place, or NULL if the copy is not wanted. It is
 * left partly written if the encoder falls back to RAW.
 *
 * Returns bytes written and stores the chosen mode in *mode. Returns 0 with
 * *mode set to RAW when neither predictor fits `capacity`; the caller then
 * transmits APP_FRAME_BYTES from the frame itself.
 */
size_t frame_codec_encode(const uint16_t *frame, const uint16_t *reference,
                          uint8_t *out, size_t capacity, uint8_t *mode,
                          uint16_t *capture);

/* Reconstruct `frame` from `in`. `reference` must be the previous frame for
 * INTER and is ignored otherwise. RAW expects APP_FRAME_BYTES of pixel data
 * with no preamble. Returns false if the stream is truncated, malformed, or
 * the checksum does not match the reconstruction. */
bool frame_codec_decode(const uint8_t *in, size_t length, uint8_t mode,
                        const uint16_t *reference, uint16_t *frame);

/* Exposed so the link layer and the tests agree on the value. */
uint32_t frame_codec_checksum(const uint16_t *frame);
