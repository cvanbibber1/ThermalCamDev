#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
  DOSIMETER_FLAG_NOMINAL_CALIBRATION = 0x01U,
  DOSIMETER_FLAG_SATURATED = 0x02U,
  DOSIMETER_FLAG_STALE = 0x04U,
};

typedef struct {
  uint32_t timestamp_ms;
  uint16_t raw_mean;
  uint16_t raw_min;
  uint16_t raw_max;
  uint16_t raw_stddev;
  uint32_t vdda_mv;
  uint32_t voltage_uv;
  uint32_t filtered_voltage_uv;
  uint32_t radiation_millirad;
  uint32_t flags;
} dosimeter_snapshot_t;

bool dosimeter_init(void);
void dosimeter_task(void);
void dosimeter_get_snapshot(dosimeter_snapshot_t *snapshot);

