#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LEPTON_CCI_MAX_WORDS 512U

enum {
  LEPTON_CCI_OK = 0,
  LEPTON_CCI_I2C_ERROR = -100,
  LEPTON_CCI_TIMEOUT = -101,
  LEPTON_CCI_RANGE = -102,
  LEPTON_CCI_PROTOCOL = -103,
};

#define LEPTON_CID_SYS_PING 0x0200U
#define LEPTON_CID_SYS_SERIAL_NUMBER 0x0208U
#define LEPTON_CID_SYS_TELEMETRY_ENABLE 0x0218U
#define LEPTON_CID_SYS_FFC_SHUTTER_MODE 0x023CU
#define LEPTON_CID_SYS_RUN_FFC 0x0242U
#define LEPTON_CID_SYS_FFC_STATUS 0x0244U
#define LEPTON_CID_OEM_PART_NUMBER 0x481CU
#define LEPTON_CID_OEM_SOFTWARE_VERSION 0x4820U
#define LEPTON_CID_OEM_VIDEO_FORMAT 0x4828U
#define LEPTON_CID_OEM_GPIO_MODE 0x4854U
#define LEPTON_CID_RAD_TLINEAR_ENABLE 0x4EC0U
#define LEPTON_CID_RAD_TLINEAR_RESOLUTION 0x4EC4U

int lepton_cci_read_register(uint16_t address, uint16_t *value);
int lepton_cci_write_register(uint16_t address, uint16_t value);
int lepton_cci_get(uint16_t command_id, uint16_t *words, uint16_t *word_count);
int lepton_cci_set(uint16_t command_id, const uint16_t *words, uint16_t word_count);
int lepton_cci_run(uint16_t command_id);
bool lepton_cci_booted(void);
int lepton_cci_configure_radiometric(void);

/* Shutter-mode object fields the firmware and host care about. The Lepton runs
 * flat-field correction itself once auto mode is configured; the firmware only
 * needs to set the policy and report progress. */
#define LEPTON_FFC_SHUTTER_MODE_MANUAL 0U
#define LEPTON_FFC_SHUTTER_MODE_AUTO 1U
#define LEPTON_FFC_SHUTTER_MODE_EXTERNAL 2U

typedef struct {
  uint32_t shutter_mode;
  uint32_t temp_lockout_state;
  uint32_t elapsed_since_ffc_ms;
  uint32_t desired_period_ms;
  uint16_t desired_temp_delta;
  int16_t ffc_state;
} lepton_ffc_status_t;

int lepton_cci_get_ffc_status(lepton_ffc_status_t *status);
