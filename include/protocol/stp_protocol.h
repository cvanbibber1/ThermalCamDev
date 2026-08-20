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

/* This experiment's assigned Target ID. */
#ifndef STP_DEFAULT_TARGET_ID
#define STP_DEFAULT_TARGET_ID 0xC7U
#endif

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
