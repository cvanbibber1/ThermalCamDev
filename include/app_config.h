#pragma once

#include <stdint.h>

#define APP_FW_VERSION_MAJOR 0U
#define APP_FW_VERSION_MINOR 2U
#define APP_FW_VERSION_PATCH 0U

#define APP_SYSCLK_HZ 100000000U
#define APP_USBCLK_HZ 48000000U
#define APP_LEPTON_I2C_ADDRESS 0x2AU
#define APP_LEPTON_I2C_HZ 100000U
#define APP_LEPTON_I2C_TIMEOUT_MS 1000U
#define APP_LEPTON_CCI_EARLIEST_MS 950U
#define APP_LEPTON_BOOT_DEADLINE_MS 5000U
/* Flat-field correction policy. The Lepton closes its own shutter once auto
 * mode is configured, whichever of these two thresholds is reached first.
 * These are the module defaults, set explicitly so behaviour does not depend
 * on how a particular camera was left configured. */
#define APP_LEPTON_FFC_PERIOD_MS 180000U
#define APP_LEPTON_FFC_TEMP_DELTA_C100 150U
/* How often the firmware checks that the camera is still correcting itself.
 * Each check is a blocking 100 kHz CCI read, so keep it well apart. */
#define APP_LEPTON_FFC_CHECK_MS 30000U
/* Slack past the desired period before the firmware forces a correction. */
#define APP_LEPTON_FFC_OVERDUE_MS 60000U
#define APP_LEPTON_RESYNC_MS 200U
/* Longest a ready chunk may wait for HAL/DMA to release before resyncing. */
#define APP_LEPTON_CHUNK_STALL_MS 5U
/* A chunk clocks out in about 13 ms; past this the transport is dead. */
#define APP_LEPTON_CHUNK_TIMEOUT_MS 50U
#define APP_RS485_BAUD 921600U
#define APP_NODE_ADDRESS_DEFAULT 1U
#define APP_DOSIMETER_UV_PER_RAD 2500U

#define APP_FRAME_WIDTH 160U
#define APP_FRAME_HEIGHT 120U
#define APP_FRAME_PIXELS (APP_FRAME_WIDTH * APP_FRAME_HEIGHT)
#define APP_FRAME_BYTES (APP_FRAME_PIXELS * 2U)
/* Longest a chunked reader may pin the published frame. */
#define APP_FRAME_HOLD_MS 2000U
