#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* DICE <-> Experiment RS-422 packet format for the STP flight.
 *
 * Framing, sizes, type codes and CRC coverage come from
 * dice_experiment_rs422_protocol.md and rs422_command_packet_format.md and are
 * authoritative. Four things are NOT defined by those documents and are
 * gathered in the "unconfirmed" block below so they can be corrected in one
 * place once the interface control document is available: wire byte order,
 * CRC-16 parameters, the coarse-time epoch, and our Target ID. The payload
 * contents of LRT and HRT are also undefined by the specification, so the
 * layouts here are this experiment's own and must be agreed with DICE.
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
#define STP_LRT_DATA_SIZE 1256U
#define STP_LRT_PAYLOAD_SIZE 1248U
#define STP_HRT_DATA_SIZE 1288U
#define STP_HRT_PAYLOAD_SIZE 1280U

/* ---------------------------------------------------------- unconfirmed -- */

/* The tables show the sync as the value 0x1ACF_FC1D without stating how it is
 * placed on the wire. Most significant byte first gives 1A CF FC 1D. */
#ifndef STP_BIG_ENDIAN
#define STP_BIG_ENDIAN 1
#endif

/* The tables state a 16-bit CRC without parameters. CRC-16/CCITT-FALSE with a
 * 0xFFFF seed is the usual choice in this class of avionics link. */
#ifndef STP_CRC16_SEED
#define STP_CRC16_SEED 0xFFFFU
#endif

/* Assigned by the flight computer; the specification does not allocate it. */
#ifndef STP_DEFAULT_TARGET_ID
#define STP_DEFAULT_TARGET_ID 0x01U
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
