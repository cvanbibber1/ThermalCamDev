#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
  DOSIMETER_FLAG_NOMINAL_CALIBRATION = 0x01U,
  DOSIMETER_FLAG_SATURATED = 0x02U,
  DOSIMETER_FLAG_STALE = 0x04U,
  /* A zero capture is averaging and the reference is not settled yet. */
  DOSIMETER_FLAG_ZEROING = 0x08U,
  /* No zero has ever been stored, so the dose is relative to zero volts and
   * is not a real dose reading. */
  DOSIMETER_FLAG_UNZEROED = 0x10U,
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
  /* Reference captured with no dose applied; dose is measured from here. */
  int32_t zero_voltage_uv;
  /* Signed: an input below the stored zero reads negative rather than
   * wrapping, which matters while the detector is under-driven. */
  int32_t dose_microrad;
  uint32_t flags;
} dosimeter_snapshot_t;

bool dosimeter_init(void);
void dosimeter_task(void);
void dosimeter_get_snapshot(dosimeter_snapshot_t *snapshot);

/* Average the next APP_DOSIMETER_ZERO_SAMPLES readings and store the result as
 * the zero reference. Returns false if a capture is already running. */
bool dosimeter_begin_zero(void);
bool dosimeter_zero_in_progress(void);
/* Set the reference directly, for restoring a known calibration. */
void dosimeter_set_zero(int32_t microvolts);

