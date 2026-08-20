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
#define APP_LEPTON_FFC_PERIOD_MS 60000U
#define APP_LEPTON_FFC_TEMP_DELTA_C100 150U
/* How often the firmware checks whether a correction is due. Each check is a
 * blocking 100 kHz CCI read, so it is kept well apart, but it must be short
 * against the period above or corrections drift late. */
#define APP_LEPTON_FFC_CHECK_MS 10000U
/* The shutter takes about a second, and the first frames after it are not
 * settled. Captures that ask for a correction first wait this long. */
#define APP_LEPTON_FFC_SETTLE_MS 1500U
#define APP_LEPTON_RESYNC_MS 200U
/* Longest a ready chunk may wait for HAL/DMA to release before resyncing. */
#define APP_LEPTON_CHUNK_STALL_MS 5U
/* A chunk clocks out in about 13 ms; past this the transport is dead. */
#define APP_LEPTON_CHUNK_TIMEOUT_MS 50U
#define APP_RS485_BAUD 921600U
/* Doubles as the STP Target ID on this branch; see STP_DEFAULT_TARGET_ID. */
#define APP_NODE_ADDRESS_DEFAULT 0xC7U
/* Dosimeter transfer function, in volts at PA4 after the external gain stage:
 *
 *     DOSI = 0.1575 + 0.0025 * D_rad
 *
 * so dose is the offset above the 157.5 mV intercept divided by 2.5 mV per
 * rad. The intercept is the nominal value for the part; capturing a zero
 * measures it for this particular unit and replaces it. */
#define APP_DOSIMETER_INTERCEPT_UV 157500U
#define APP_DOSIMETER_UV_PER_RAD 2500U
/* Samples averaged when capturing a new zero. The ADC block rate is about
 * 15 Hz, so this settles the reference over roughly two seconds instead of
 * trusting one noisy reading. */
#define APP_DOSIMETER_ZERO_SAMPLES 32U
/* Exponential filter depth on the dosimeter voltage, as a right shift, so the
 * weight is 1/2^N per ADC block. With the detector under-driven the signal
 * sits near the bottom of the range where one ADC count is about 730 uV, or
 * 0.29 rad, so the reading needs heavy averaging to be usable at all. At the
 * 15.6 Hz block rate a shift of 5 gives roughly a two second time constant. */
#define APP_DOSIMETER_FILTER_SHIFT 5U

#define APP_FRAME_WIDTH 160U
#define APP_FRAME_HEIGHT 120U
#define APP_FRAME_PIXELS (APP_FRAME_WIDTH * APP_FRAME_HEIGHT)
#define APP_FRAME_BYTES (APP_FRAME_PIXELS * 2U)
/* Longest a chunked reader may pin the published frame. */
#define APP_FRAME_HOLD_MS 2000U
