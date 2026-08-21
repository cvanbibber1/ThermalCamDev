#pragma once

#include <stdint.h>

typedef struct {
  uint32_t reset_cause;
  uint32_t fatal_code;
  uint32_t clock_failures;
  uint32_t camera_boot_failures;
  uint32_t cci_errors;
  uint32_t ffc_forced_runs;
  uint32_t vospi_discard_packets;
  uint32_t vospi_crc_errors;
  uint32_t vospi_sequence_errors;
  uint32_t vospi_resyncs;
  uint32_t vospi_start_retries;
  uint32_t vospi_link_stalls;
  uint32_t vospi_start_failures;
  uint32_t vospi_spi_errors;
  uint32_t vospi_chunks;
  uint32_t vospi_segments;
  uint32_t vospi_segments_ignored;
  uint32_t frames_complete;
  uint32_t frames_dropped;
  uint32_t usb_rx_overruns;
  uint32_t usb_tx_busy;
  uint32_t adc_overruns;
  uint32_t rs485_rx_overruns;
  uint32_t rs485_crc_errors;
  uint32_t rs485_tx_busy;
  /* Frames the encoder could not fit, sent uncompressed instead. */
  uint32_t codec_raw_fallback;
  /* Keyframes emitted, whether on the interval or forced by a gap. */
  uint32_t codec_keyframes;
  /* Longest encode seen, in microseconds. The VoSPI DMA must be serviced every
   * 12.6 ms, so this is the margin that keeps the sensor out of resync. */
  uint32_t codec_encode_us_max;
  /* Times the transmitter wanted an image chunk and the encoder had not
   * produced one yet. Distinguishes a link starved by the encoder from one
   * starved by how often the task loop comes round. */
  uint32_t codec_chunk_starved;
} health_counters_t;

extern health_counters_t g_health;
void health_increment(uint32_t *counter);

