#include "lepton_cci.h"

#include "app_config.h"
#include "board.h"
#include "health.h"

#define CCI_STATUS_REG 0x0002U
#define CCI_COMMAND_REG 0x0004U
#define CCI_DATA_LENGTH_REG 0x0006U
#define CCI_DATA_REG 0x0008U
#define CCI_DATA_BUFFER_REG 0xF800U
#define CCI_STATUS_BUSY 0x0001U
#define CCI_STATUS_BOOTED 0x0004U
#define CCI_GET_TYPE 0x0000U
#define CCI_SET_TYPE 0x0001U
#define CCI_RUN_TYPE 0x0002U

static uint8_t transfer_buffer[LEPTON_CCI_MAX_WORDS * 2U];

static int read_bytes(uint16_t address, uint8_t *data, uint16_t length) {
  HAL_StatusTypeDef status = HAL_I2C_Mem_Read(&hi2c1, APP_LEPTON_I2C_ADDRESS << 1,
                                              address, I2C_MEMADD_SIZE_16BIT,
                                              data, length, APP_LEPTON_I2C_TIMEOUT_MS);
  if (status != HAL_OK) {
    health_increment(&g_health.cci_errors);
    return LEPTON_CCI_I2C_ERROR;
  }
  return LEPTON_CCI_OK;
}

static int write_bytes(uint16_t address, const uint8_t *data, uint16_t length) {
  HAL_StatusTypeDef status = HAL_I2C_Mem_Write(&hi2c1, APP_LEPTON_I2C_ADDRESS << 1,
                                               address, I2C_MEMADD_SIZE_16BIT,
                                               (uint8_t *)data, length,
                                               APP_LEPTON_I2C_TIMEOUT_MS);
  if (status != HAL_OK) {
    health_increment(&g_health.cci_errors);
    return LEPTON_CCI_I2C_ERROR;
  }
  return LEPTON_CCI_OK;
}

int lepton_cci_read_register(uint16_t address, uint16_t *value) {
  uint8_t bytes[2];
  if (value == NULL) {
    return LEPTON_CCI_RANGE;
  }
  int result = read_bytes(address, bytes, sizeof(bytes));
  if (result == LEPTON_CCI_OK) {
    *value = ((uint16_t)bytes[0] << 8) | bytes[1];
  }
  return result;
}

int lepton_cci_write_register(uint16_t address, uint16_t value) {
  const uint8_t bytes[2] = {(uint8_t)(value >> 8), (uint8_t)value};
  return write_bytes(address, bytes, sizeof(bytes));
}

static int wait_ready(uint32_t timeout_ms, uint16_t *final_status) {
  uint32_t start = HAL_GetTick();
  uint16_t status = 0U;
  do {
    int result = lepton_cci_read_register(CCI_STATUS_REG, &status);
    if (result != LEPTON_CCI_OK) {
      return result;
    }
    if ((status & CCI_STATUS_BUSY) == 0U) {
      if (final_status != NULL) {
        *final_status = status;
      }
      int8_t camera_result = (int8_t)(status >> 8);
      return camera_result == 0 ? LEPTON_CCI_OK : camera_result;
    }
  } while ((HAL_GetTick() - start) < timeout_ms);
  health_increment(&g_health.cci_errors);
  return LEPTON_CCI_TIMEOUT;
}

static int words_to_camera(uint16_t address, const uint16_t *words, uint16_t count) {
  if ((count > LEPTON_CCI_MAX_WORDS) || ((words == NULL) && (count != 0U))) {
    return LEPTON_CCI_RANGE;
  }
  for (uint16_t i = 0U; i < count; ++i) {
    transfer_buffer[i * 2U] = (uint8_t)(words[i] >> 8);
    transfer_buffer[i * 2U + 1U] = (uint8_t)words[i];
  }
  return write_bytes(address, transfer_buffer, (uint16_t)(count * 2U));
}

static int words_from_camera(uint16_t address, uint16_t *words, uint16_t count) {
  if ((count > LEPTON_CCI_MAX_WORDS) || ((words == NULL) && (count != 0U))) {
    return LEPTON_CCI_RANGE;
  }
  int result = read_bytes(address, transfer_buffer, (uint16_t)(count * 2U));
  if (result != LEPTON_CCI_OK) {
    return result;
  }
  for (uint16_t i = 0U; i < count; ++i) {
    words[i] = ((uint16_t)transfer_buffer[i * 2U] << 8) |
               transfer_buffer[i * 2U + 1U];
  }
  return LEPTON_CCI_OK;
}

int lepton_cci_get(uint16_t command_id, uint16_t *words, uint16_t *word_count) {
  if ((words == NULL) || (word_count == NULL) || (*word_count == 0U) ||
      (*word_count > LEPTON_CCI_MAX_WORDS)) {
    return LEPTON_CCI_RANGE;
  }
  int result = wait_ready(APP_LEPTON_I2C_TIMEOUT_MS, NULL);
  if (result != LEPTON_CCI_OK) {
    return result;
  }
  result = lepton_cci_write_register(CCI_COMMAND_REG, command_id | CCI_GET_TYPE);
  if (result == LEPTON_CCI_OK) {
    result = wait_ready(APP_LEPTON_I2C_TIMEOUT_MS, NULL);
  }
  uint16_t returned_bytes = 0U;
  if (result == LEPTON_CCI_OK) {
    result = lepton_cci_read_register(CCI_DATA_LENGTH_REG, &returned_bytes);
  }
  if ((result != LEPTON_CCI_OK) || (returned_bytes == 0U) ||
      ((returned_bytes & 1U) != 0U)) {
    return result == LEPTON_CCI_OK ? LEPTON_CCI_PROTOCOL : result;
  }
  uint16_t returned_words = returned_bytes / 2U;
  if (returned_words > *word_count) {
    return LEPTON_CCI_PROTOCOL;
  }
  *word_count = returned_words;
  return words_from_camera(returned_bytes <= 32U ? CCI_DATA_REG : CCI_DATA_BUFFER_REG,
                           words, returned_words);
}

int lepton_cci_set(uint16_t command_id, const uint16_t *words, uint16_t word_count) {
  if ((words == NULL) || (word_count == 0U) || (word_count > LEPTON_CCI_MAX_WORDS)) {
    return LEPTON_CCI_RANGE;
  }
  int result = wait_ready(APP_LEPTON_I2C_TIMEOUT_MS, NULL);
  if (result == LEPTON_CCI_OK) {
    result = words_to_camera(word_count <= 16U ? CCI_DATA_REG : CCI_DATA_BUFFER_REG,
                             words, word_count);
  }
  if (result == LEPTON_CCI_OK) {
    result = lepton_cci_write_register(CCI_DATA_LENGTH_REG, (uint16_t)(word_count * 2U));
  }
  if (result == LEPTON_CCI_OK) {
    result = lepton_cci_write_register(CCI_COMMAND_REG, command_id | CCI_SET_TYPE);
  }
  return result == LEPTON_CCI_OK
             ? wait_ready(APP_LEPTON_I2C_TIMEOUT_MS, NULL)
             : result;
}

int lepton_cci_run(uint16_t command_id) {
  int result = wait_ready(APP_LEPTON_I2C_TIMEOUT_MS, NULL);
  if (result == LEPTON_CCI_OK) {
    result = lepton_cci_write_register(CCI_DATA_LENGTH_REG, 0U);
  }
  if (result == LEPTON_CCI_OK) {
    result = lepton_cci_write_register(CCI_COMMAND_REG, command_id | CCI_RUN_TYPE);
  }
  return result == LEPTON_CCI_OK
             ? wait_ready(1000U, NULL)
             : result;
}

bool lepton_cci_booted(void) {
  uint16_t status = 0U;
  return (lepton_cci_read_register(CCI_STATUS_REG, &status) == LEPTON_CCI_OK) &&
         ((status & CCI_STATUS_BOOTED) != 0U) && ((status & CCI_STATUS_BUSY) == 0U);
}

/* FLR_SYS_FFC_SHUTTER_MODE_OBJ_T, sixteen 16-bit words, each 32-bit field low
 * word first: shutterMode, tempLockoutState, videoFreezeDuringFFC, ffcDesired,
 * elapsedTimeSinceLastFfc, desiredFfcPeriod, explicitCmdToOpen, then the
 * 16-bit desiredFfcTempDelta and imminentDelay. */
#define FFC_OBJ_WORDS 16U
#define FFC_SHUTTER_MODE_AUTO 1U
#define FFC_IMMINENT_DELAY_DEFAULT 52U

/* The shutter-mode object is deliberately never written.
 *
 * A 32-byte block write to the DATA registers is accepted by this module
 * (STATUS reports success) but only part of it lands, leaving the object
 * holding a mix of written and stale words - which disables automatic FFC
 * silently. Verified on 2026-08-18: a clean boot reads a correct object, and
 * the same object reads back corrupted immediately after the firmware writes
 * it, while a 16-word GET of a never-written object (OEM part number) is
 * always correct.
 *
 * The module already powers up in exactly the policy this design wants -
 * auto shutter, 180 s period, 1.5 C delta - and the Lepton is power-cycled on
 * every MCU reset, so the defaults are re-established each boot. The firmware
 * therefore reads the policy and supervises it, falling back to the RUN FFC
 * command (which carries no data payload and is reliable) if the camera is
 * not correcting itself. */

int lepton_cci_get_ffc_status(lepton_ffc_status_t *status) {
  if (status == NULL) {
    return LEPTON_CCI_RANGE;
  }
  uint16_t object[FFC_OBJ_WORDS];
  uint16_t count = FFC_OBJ_WORDS;
  int result = lepton_cci_get(LEPTON_CID_SYS_FFC_SHUTTER_MODE, object, &count);
  if (result != LEPTON_CCI_OK) {
    return result;
  }
  if (count < FFC_OBJ_WORDS) {
    return LEPTON_CCI_PROTOCOL;
  }
  status->shutter_mode = (uint32_t)object[0] | ((uint32_t)object[1] << 16);
  status->temp_lockout_state = (uint32_t)object[2] | ((uint32_t)object[3] << 16);
  status->elapsed_since_ffc_ms = (uint32_t)object[8] | ((uint32_t)object[9] << 16);
  status->desired_period_ms = (uint32_t)object[10] | ((uint32_t)object[11] << 16);
  status->desired_temp_delta = object[14];

  uint16_t state[2] = {0U, 0U};
  uint16_t state_count = 2U;
  status->ffc_state = 0;
  if (lepton_cci_get(LEPTON_CID_SYS_FFC_STATUS, state, &state_count) ==
      LEPTON_CCI_OK) {
    status->ffc_state = (int16_t)state[0];
  }
  return LEPTON_CCI_OK;
}

int lepton_cci_configure_radiometric(void) {
  const uint16_t disabled[2] = {0U, 0U};
  /* Lepton CCI returns and accepts 32-bit scalar attributes low-word first. */
  const uint16_t enabled[2] = {1U, 0U};
  const uint16_t raw14[2] = {7U, 0U};
  const uint16_t vsync[2] = {5U, 0U};
  int result = lepton_cci_set(LEPTON_CID_SYS_TELEMETRY_ENABLE, disabled, 2U);
  if (result == LEPTON_CCI_OK) {
    result = lepton_cci_set(LEPTON_CID_OEM_VIDEO_FORMAT, raw14, 2U);
  }
  if (result == LEPTON_CCI_OK) {
    result = lepton_cci_set(LEPTON_CID_RAD_TLINEAR_ENABLE, enabled, 2U);
  }
  if (result == LEPTON_CCI_OK) {
    result = lepton_cci_set(LEPTON_CID_RAD_TLINEAR_RESOLUTION, enabled, 2U);
  }
  if (result == LEPTON_CCI_OK) {
    result = lepton_cci_set(LEPTON_CID_OEM_GPIO_MODE, vsync, 2U);
  }
  return result;
}
