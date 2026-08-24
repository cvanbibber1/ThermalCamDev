#include "settings.h"

#include "app_config.h"
#include "board.h"
#include "protocol/crc.h"

#include <string.h>

/* The last 128 KiB sector. Firmware occupies the low sectors and this one is
 * far enough away that growth cannot reach it. */
#define SETTINGS_SECTOR FLASH_SECTOR_11
#define SETTINGS_ADDRESS 0x080E0000U

#define SETTINGS_MAGIC 0x314D4854U /* "THM1" */
#define SETTINGS_VERSION 1U

typedef struct {
  uint32_t magic;
  uint32_t version;
  int32_t dosimeter_zero_uv;
  uint8_t node_address;
  uint8_t reserved[3];
  uint32_t crc;
} stored_settings_t;

static settings_t active;
static int status = SETTINGS_DEFAULTED;
static bool save_requested;
static uint32_t save_count;

static void load_defaults(void) {
  active.dosimeter_zero_uv = 0;
  active.node_address = APP_NODE_ADDRESS_DEFAULT;
}

static uint32_t record_crc(const stored_settings_t *record) {
  return crc32c((const uint8_t *)record,
                sizeof(*record) - sizeof(record->crc));
}

void settings_init(void) {
  load_defaults();
  const stored_settings_t *record = (const stored_settings_t *)SETTINGS_ADDRESS;
  if ((record->magic != SETTINGS_MAGIC) || (record->version != SETTINGS_VERSION) ||
      (record->crc != record_crc(record))) {
    /* Erased or from another build: keep the defaults and leave the sector
     * alone until something is deliberately saved. */
    status = SETTINGS_DEFAULTED;
    return;
  }
  active.dosimeter_zero_uv = record->dosimeter_zero_uv;
  active.node_address = record->node_address;
  status = SETTINGS_OK;
}

const settings_t *settings_get(void) { return &active; }

int settings_status(void) { return status; }

uint32_t settings_save_count(void) { return save_count; }

bool settings_set_dosimeter_zero(int32_t microvolts) {
  if (active.dosimeter_zero_uv == microvolts) {
    return false;
  }
  active.dosimeter_zero_uv = microvolts;
  save_requested = true;
  status = SETTINGS_SAVE_PENDING;
  return true;
}

bool settings_set_node_address(uint8_t address) {
  if (active.node_address == address) {
    return false;
  }
  active.node_address = address;
  save_requested = true;
  status = SETTINGS_SAVE_PENDING;
  return true;
}

static int write_record(void) {
  stored_settings_t record;
  memset(&record, 0, sizeof(record));
  record.magic = SETTINGS_MAGIC;
  record.version = SETTINGS_VERSION;
  record.dosimeter_zero_uv = active.dosimeter_zero_uv;
  record.node_address = active.node_address;
  record.crc = record_crc(&record);

  /* A sector erase blocks the core, and the datasheet allows it up to three
   * seconds. Refresh first so the whole watchdog period is available for it
   * rather than whatever happened to be left. */
  board_watchdog_refresh();
  HAL_FLASH_Unlock();

  FLASH_EraseInitTypeDef erase = {0};
  erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase.Sector = SETTINGS_SECTOR;
  erase.NbSectors = 1U;
  erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
  uint32_t failing_sector = 0U;
  if (HAL_FLASHEx_Erase(&erase, &failing_sector) != HAL_OK) {
    HAL_FLASH_Lock();
    return SETTINGS_ERASE_FAILED;
  }

  const uint32_t *words = (const uint32_t *)&record;
  for (size_t index = 0U; index < (sizeof(record) / sizeof(uint32_t)); ++index) {
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                          SETTINGS_ADDRESS + (index * sizeof(uint32_t)),
                          words[index]) != HAL_OK) {
      HAL_FLASH_Lock();
      return SETTINGS_PROGRAM_FAILED;
    }
  }

  HAL_FLASH_Lock();

  const stored_settings_t *stored = (const stored_settings_t *)SETTINGS_ADDRESS;
  if (memcmp(stored, &record, sizeof(record)) != 0) {
    return SETTINGS_PROGRAM_FAILED;
  }
  return SETTINGS_OK;
}

void settings_task(void) {
  if (!save_requested) {
    return;
  }
  save_requested = false;
  /* The core cannot fetch instructions while the sector erases, so everything
   * else stops for the duration. VoSPI reacquires on its own afterwards. */
  status = write_record();
  if (status == SETTINGS_OK) {
    ++save_count;
  }
}
