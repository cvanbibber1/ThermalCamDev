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
} health_counters_t;

extern health_counters_t g_health;
void health_increment(uint32_t *counter);

