#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Persistent configuration held in the last flash sector.
 *
 * Writing flash stalls the core for the duration of the sector erase, which on
 * this part is over a second, so saves are deferred to the task loop and only
 * performed when a value actually changed. Callers get their response before
 * the stall and read the outcome back afterwards through settings_status().
 */

enum {
  SETTINGS_OK = 0,
  SETTINGS_DEFAULTED = 1,
  SETTINGS_SAVE_PENDING = 2,
  SETTINGS_ERASE_FAILED = -1,
  SETTINGS_PROGRAM_FAILED = -2,
};

typedef struct {
  /* Dosimeter output with no dose applied. Dose is derived from the change
   * away from this reference, so it is captured with the source removed and
   * then left alone until deliberately re-zeroed. */
  int32_t dosimeter_zero_uv;
  uint8_t node_address;
} settings_t;

void settings_init(void);
void settings_task(void);

const settings_t *settings_get(void);
int settings_status(void);
/* Number of completed saves, so a host can confirm one landed. */
uint32_t settings_save_count(void);

/* Stage a value and request a save. Both return false if nothing changed, in
 * which case no flash write is scheduled. */
bool settings_set_dosimeter_zero(int32_t microvolts);
bool settings_set_node_address(uint8_t address);
