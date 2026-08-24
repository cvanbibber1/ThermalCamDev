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
/* Longest the sensor may go without completing a frame before the firmware
 * stops believing it.
 *
 * The transport can look perfectly healthy while the sensor produces nothing
 * but discard packets: chunks keep arriving on time, so none of the timeouts
 * above notice. Observed on the bench, and it never recovered, because
 * re-deriving the bit alignment is not an escape from a sensor that has
 * stopped. In orbit there is nobody to power cycle the board.
 *
 * Five seconds is about forty missed frames, and far longer than the ~1 s the
 * shutter takes, so a correction cannot trip it. */
#define APP_LEPTON_FRAME_STALL_MS 5000U
/* Resyncs to attempt before power cycling the sensor instead. A resync only
 * reacquires the link; if the sensor itself is wedged, only power fixes it. */
#define APP_LEPTON_STALL_RESYNCS 3U
/* Longest a ready chunk may wait for HAL/DMA to release before resyncing. */
#define APP_LEPTON_CHUNK_STALL_MS 5U
/* A chunk clocks out in about 13 ms; past this the transport is dead. */
#define APP_LEPTON_CHUNK_TIMEOUT_MS 50U
/* RS-422 line rate.
 *
 * THIS BRANCH IS A TEST BRANCH AND DOES NOT MATCH FLIGHT. Flight runs at
 * 921600 on flight-test/rs422-compressed; this raises the rate to find out
 * what the camera can do when the link stops being the limit. The flight
 * computer has to agree, so do not fly this without checking that it can.
 *
 * Pick rates the hardware can hit exactly. USART2 is clocked from APB1 at
 * 50 MHz and divides by BRR/16, so only some rates land on an integer:
 *
 *     921600   BRR 54.25  0.5% error   the flight rate
 *   1000000    BRR 50     exact
 *   1500000    BRR 33.33  1.0% error
 *   2000000    BRR 25     exact        <- this branch
 *   3000000    BRR 16.67  2.0% error   too far; both ends would disagree
 *
 * 2,000,000 is also one of the FT232R's exact rates, so neither end is
 * approximating. The absolute ceiling is 3.125 Mbaud, being APB1 over 16.
 */
#define APP_RS485_BAUD 2000000U
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

/* ------------------------------------------------------- image compression -- */

/* Compress image frames before they go out over RS-422. The link carries
 * 92,160 bytes a second and a raw frame is 38,400, so uncompressed video is
 * capped near 2.3 frames a second against a sensor that produces 8.7.
 * Set to 0 to transmit raw frames, which is the pre-compression behaviour. */
#define APP_CODEC_ENABLED 1

/* Frames between keyframes.
 *
 * A difference frame cannot be decoded unless the one before it arrived, so a
 * single lost packet costs every frame up to the next keyframe. This is the
 * bound on that damage: at the rate the link actually achieves, 12 frames is
 * about two seconds. One dropped packet on the bench cost 24 consecutive
 * frames with the interval at 24.
 *
 * Shorter recovers faster and costs bandwidth, because a keyframe is roughly
 * 1.7 times the size of a difference frame; halving the interval from 24 costs
 * about 2%. */
#define APP_CODEC_GOP 12U

/* Encoder output buffer. Measured over 50 real frames: inter averages 9.7 kB
 * and the largest keyframe was 16.3 kB, so this is about a 20% margin over the
 * worst case seen. A frame that will not fit is sent uncompressed instead, so
 * overflowing this costs frame rate and nothing else. */
#define APP_CODEC_BUFFER_BYTES 20480U

/* Rows encoded per slice, and how long the encoder may keep the core before
 * giving it back.
 *
 * The VoSPI DMA must be serviced every 12.6 ms or the sensor drops into
 * resync, so the budget sits well under that. The task loop does not come back
 * often enough to finish a frame one slice per call -- that stretched a 33 ms
 * encode to about 120 ms of wall clock and cost half the frame rate -- so
 * slices repeat within a call until the budget is spent. */
#define APP_CODEC_ROWS_PER_STEP 12U
#define APP_CODEC_STEP_BUDGET_US 8000U
