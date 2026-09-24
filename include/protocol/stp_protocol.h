#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* DICE <-> Experiment RS-422 packet format for the STP flight.
 *
 * Framing, sizes, type codes and CRC coverage come from
 * dice_experiment_rs422_protocol.md and rs422_command_packet_format.md.
 *
 * Byte order, CRC parameters, CRC placement and our Target ID were confirmed
 * by the project on 2026-08-19 and are no longer assumptions. What remains
 * open is the coarse-time epoch, the internal structure of the 105-byte
 * command payload, and whether DICE expects a particular layout inside the
 * LRT and HRT payloads; the layouts here are this experiment's own.
 */

/* ---------------------------------------------------------------- sizes -- */

#define STP_SYNC_WORD 0x1ACFFC1DU

#define STP_TYPE_COMMAND 0x10U
#define STP_TYPE_LRT 0x81U
#define STP_TYPE_HRT_STOP 0x85U
#define STP_TYPE_HRT_STOP_WITH_LOSS 0x86U
#define STP_TYPE_HRT_GO 0x87U
/* Transmitted data reuses the request type codes; direction and size are what
 * distinguish them, as the specification warns. */
#define STP_TYPE_LRT_DATA STP_TYPE_LRT
#define STP_TYPE_HRT_DATA STP_TYPE_HRT_GO

/* DICE -> Experiment */
#define STP_COMMAND_SIZE 120U
#define STP_COMMAND_PAYLOAD_SIZE 105U
#define STP_COMMAND_PAYLOAD_OFFSET 12U
/* Offsets within the command payload itself. */
#define STP_CMD_SEQ_OFFSET 1U
#define STP_CMD_ARGLEN_OFFSET 3U
#define STP_CMD_FORCE_OFFSET 4U
#define STP_CMD_CRC_OFFSET 5U
#define STP_CMD_ARGS_OFFSET 7U
#define STP_CMD_ARGS_MAX 98U
#define STP_REQUEST_SIZE 14U
/* Every received packet starts with the same 12-byte preamble, so the type at
 * offset 10 can be read before the total length is known. */
#define STP_RX_HEADER_SIZE 12U
#define STP_RX_MAX_SIZE STP_COMMAND_SIZE

/* Experiment -> DICE */
#define STP_ACK_SIZE 8U
/* 1256 total: 6 header, 1248 payload, 2 CRC. The source table listed only
 * 1254 bytes; the missing two are the CRC, confirmed 2026-08-19. */
#define STP_LRT_DATA_SIZE 1256U
#define STP_LRT_PAYLOAD_SIZE 1248U
#define STP_HRT_DATA_SIZE 1288U
#define STP_HRT_PAYLOAD_SIZE 1280U

/* ------------------------------------------------------------ confirmed -- */

/* Big endian: the sync value 0x1ACF_FC1D goes on the wire as 1A CF FC 1D, and
 * every multi-byte field follows. */
#ifndef STP_BIG_ENDIAN
#define STP_BIG_ENDIAN 1
#endif

/* CRC-16/CCITT-FALSE: polynomial 0x1021, seed 0xFFFF, no reflection, no final
 * xor. It occupies the last two bytes of every packet in both directions,
 * which is what resolves the two bytes the LRT table did not account for. */
#ifndef STP_CRC16_SEED
#define STP_CRC16_SEED 0xFFFFU
#endif

/* The Target ID of the whole RS-422 setup.
 *
 * It names the system, not a device: every camera on this bus answers to
 * 0xC7, and all of them therefore see every packet. What tells them apart is
 * the camera index carried inside the command payload.
 *
 * The consequence is that exactly one camera may transmit at a time, which is
 * what BUS_SELECT_CAMERA arbitrates. Two drivers on one pair corrupt each
 * other's packets and neither side is told why. */
#ifndef STP_DEFAULT_TARGET_ID
#define STP_DEFAULT_TARGET_ID 0xC7U
#endif

/* -------------------------------------------------- experiment commands -- */

/* Command payload, version 3, matching the visual payload's encoder so one
 * host can drive both:
 *
 *   byte 0      opcode
 *   bytes 1-2   sequence, big endian, echoed in telemetry so a reply can be
 *               matched to the command that caused it
 *   byte 3      argument length, bytes
 *   byte 4      force, non-zero to override an interlock
 *   bytes 5-6   CRC-16/CCITT-FALSE, big endian, over bytes 0 to 4 followed by
 *               the arguments. The padding is not covered.
 *   bytes 7+    arguments, little endian, zero padded to 98 bytes
 *
 * The inner CRC is not redundant with the packet CRC: it covers the command
 * alone, so a payload corrupted after the envelope was checked, or a host that
 * disagrees about the layout, is rejected rather than half obeyed.
 *
 * Layout 1 put a 16-bit parameter at byte 2 and had no inner CRC. It is not
 * accepted any more; the two cannot be told apart safely, because a layout 1
 * command's parameter bytes land where v3 keeps its length and force fields.
 *
 * Commands are deliberately discrete rather than one general "capture", so a
 * ground operator can ask for exactly one image when the link is too slow to
 * stream, or start and stop a recording, without ambiguity about what the
 * camera is currently doing. */
#define STP_CMD_NONE 0x00U
/* Liveness check, payload wide. Acknowledged and nothing else.
 *
 * This opcode used to run a flat-field correction here, which collided with
 * the visual camera's PING: a host checking the payload was alive would
 * freeze this camera's image for a second. The correction is THERMAL_NUC
 * (0x7C), which is what the combined host sends and what the camera-chain
 * specification defines. */
#define STP_CMD_PING 0x01U
/* Correct, then send exactly one complete frame and stop. For high-latency
 * links where continuous streaming is not usable. */
#define STP_CMD_TAKE_IMAGE 0x02U
/* Correct, then stream continuously until stopped. */
#define STP_CMD_START_RECORD 0x03U
#define STP_CMD_STOP_RECORD 0x04U
/* Stream without correcting first, for when the image is already settled. */
#define STP_CMD_STREAM_ON 0x05U
#define STP_CMD_STREAM_OFF 0x06U
/* Capture and store the dosimeter zero for this unit. */
#define STP_CMD_DOSIMETER_ZERO 0x07U
/* Make the next image a keyframe.
 *
 * A difference frame cannot be decoded unless the one before it arrived, so a
 * single lost packet blinds the ground until the next scheduled keyframe --
 * up to APP_CODEC_GOP frames, which is seconds of black picture for one bad
 * packet. The ground sends this the moment a frame fails to decode, and the
 * blackout becomes one round trip instead. */
#define STP_CMD_REQUEST_KEYFRAME 0x08U

/* ------------------------------------------- camera chain, protocol v1.1 -- */

/* Every camera on the bus answers to the same Target ID: it names the
 * experiment, not the device. What distinguishes them is the camera index,
 * carried inside this payload and invisible to DICE.
 *
 * The consequence is that every camera sees every packet, so exactly one may
 * transmit. BUS_SELECT_CAMERA chooses it; the rest fall silent. On the radcam
 * payload the same rule is enforced electrically, by powering one sensor at a
 * time; here it is enforced by each camera comparing the selected index with
 * its own. Two cameras driving the pair at once would corrupt both.
 *
 * 0xFF selects none, which is a legitimate quiet state rather than an error.
 * Selection does not survive a reset: the bus comes up silent, and the ground
 * or the host application selects a camera before expecting anything back.
 */
#define STP_CAMERA_NONE 0xFFU
#define STP_CAMERA_MAX 16U

#define STP_CMD_SELECT_CAMERA 0x6DU  /* index u8 */
#define STP_CMD_CAMERA_LIST 0x6EU
#define STP_CMD_CAMERA_INFO 0x61U
#define STP_CMD_CAPTURE_IMAGE 0x30U /* ephemeral HRT frame on thermal */
#define STP_CMD_COMMON_START_RECORD 0x31U /* slot recording unsupported here */
#define STP_CMD_COMMON_STOP_RECORD 0x32U
#define STP_CMD_SLOT_LIST 0x64U
#define STP_CMD_SLOT_INFO 0x65U
#define STP_CMD_SLOT_CAPTURE_IMAGE 0x66U
#define STP_CMD_SLOT_RECORD_START 0x67U
#define STP_CMD_SLOT_RECORD_STOP 0x68U
#define STP_CMD_SLOT_DOWNLOAD 0x69U
#define STP_CMD_SLOT_DELETE 0x6AU
#define STP_CMD_SLOT_DELETE_ALL 0x6BU
#define STP_CMD_SLOT_DOWNLOAD_ABORT 0x6CU
#define STP_CMD_STREAM_START 0x78U /* live HRT stream */
#define STP_CMD_STREAM_STOP 0x79U
#define STP_CMD_REQUEST_MEDIA 0x40U
#define STP_CMD_GET_MEDIA_LIST 0x21U

/* Hand the bus to one camera. index u8; 0xFF gives it to nobody.
 *
 * This exists alongside SELECT_CAMERA because the two are not the same
 * question. 0x6D is shared with the visual payload, which consumes it to
 * switch the sensor behind its own CSI lanes; it says nothing about who may
 * drive the pair. 0x6F is consumed only by cameras that are separate nodes on
 * this bus, so the host can move the right to transmit without also
 * reconfiguring another payload's internals.
 *
 * Every camera acts on it, whether or not it is the one named: the camera
 * that matches takes the bus, and every other camera drops anything it still
 * owed. Keeping them separate is what stops one host action meaning two
 * different things to two different devices. */
#define STP_CMD_BUS_SELECT_CAMERA 0x6FU

/* Thermal commands. These act on the selected camera, so a camera that is not
 * selected ignores them, and one that is not thermal reports BAD_TYPE. */
#define STP_CMD_THERMAL_SET_OUTPUT 0x74U     /* mode u8, palette u8, depth u8 */
#define STP_CMD_THERMAL_SET_RANGE 0x75U      /* mode u8, low i16, high i16 */
#define STP_CMD_THERMAL_SET_EMISSIVITY 0x76U /* emissivity u16, reflected i16 */
#define STP_CMD_THERMAL_NUC 0x7CU
#define STP_CMD_THERMAL_SPOT 0x7DU           /* x u16, y u16, w u16, h u16 */
#define STP_CMD_THERMAL_SET_PALETTE 0x7EU    /* palette u8 */

/* Only radiometric mode is emitted by this firmware. Palette and BOTH are
 * reserved values and return UNSUPPORTED until their HRT layout exists. */
#define STP_THERMAL_OUTPUT_RADIOMETRIC 0U
#define STP_THERMAL_OUTPUT_PALETTE 1U
#define STP_THERMAL_OUTPUT_BOTH 2U

#define STP_PALETTE_WHITE_HOT 0U
#define STP_PALETTE_BLACK_HOT 1U
#define STP_PALETTE_IRONBOW 2U
#define STP_PALETTE_RAINBOW 3U
#define STP_PALETTE_ARCTIC 4U
#define STP_PALETTE_COUNT 5U

/* Camera classes, reported by CAMERA_INFO and used to reject a thermal
 * command sent to a visual camera. */
#define STP_CAMERA_KIND_VISUAL 0U
#define STP_CAMERA_KIND_THERMAL 1U

/* Command results. The 8-byte acknowledgement has no room for a status, so the
 * outcome of the last command is reported in telemetry instead. */
#define STP_RESULT_OK 0U
#define STP_RESULT_BAD_PARAM 1U
#define STP_RESULT_BAD_TYPE 2U
#define STP_RESULT_CAMERA_FAULT 3U
#define STP_RESULT_NOT_SELECTED 4U
#define STP_RESULT_UNKNOWN_COMMAND 5U
#define STP_RESULT_UNSUPPORTED 6U
#define STP_RESULT_HRT_STOPPED 7U /* capture/stream refused until HRT GO */

/* ------------------------------------------------------------ receiving -- */

typedef enum {
  STP_RX_NONE = 0,
  STP_RX_COMMAND,
  STP_RX_LRT_REQUEST,
  STP_RX_HRT_STOP,
  STP_RX_HRT_STOP_WITH_LOSS,
  STP_RX_HRT_GO,
} stp_rx_kind_t;

typedef struct {
  stp_rx_kind_t kind;
  uint8_t target_id;
  uint32_t coarse_time;
  uint16_t fine_time;
  /* Command payload; NULL for the request and flow-control packets. */
  const uint8_t *payload;
  uint16_t payload_length;
} stp_rx_packet_t;

typedef struct {
  uint8_t buffer[STP_RX_MAX_SIZE];
  uint16_t length;   /* bytes accepted into buffer */
  uint16_t expected; /* total size once the type is known, 0 while unknown */
  uint8_t sync_match;
  /* Diagnostics. */
  uint32_t crc_errors;
  uint32_t type_errors;
  uint32_t accepted;
} stp_receiver_t;

void stp_receiver_init(stp_receiver_t *receiver);

/* Feed one received byte. Returns true when `packet` has been filled with a
 * structurally valid, CRC-checked packet. Target filtering is left to the
 * caller so that traffic for other experiments can be counted. */
bool stp_receiver_push(stp_receiver_t *receiver, uint8_t byte,
                       stp_rx_packet_t *packet);

/* ------------------------------------------------------------ building --- */

/* Each returns the number of bytes written, or 0 if the destination is too
 * small. Payload buffers shorter than the fixed field are zero padded. */
size_t stp_build_ack(uint8_t *out, size_t capacity, uint8_t target_id);

size_t stp_build_lrt_data(uint8_t *out, size_t capacity, uint8_t target_id,
                          const uint8_t *payload, size_t payload_length);

size_t stp_build_hrt_data(uint8_t *out, size_t capacity, uint8_t target_id,
                          const uint8_t *payload, size_t payload_length);

/* Exposed for tests and for building request packets on the host side. */
uint16_t stp_crc16(const uint8_t *data, size_t length);
void stp_put_u16(uint8_t *out, uint16_t value);
void stp_put_u32(uint8_t *out, uint32_t value);
uint16_t stp_get_u16(const uint8_t *in);
uint32_t stp_get_u32(const uint8_t *in);
