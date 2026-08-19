#include "command_dispatch.h"

#include "app_config.h"
#include "board.h"
#include "dosimeter.h"
#include "health.h"
#include "settings.h"
#include "lepton_capture.h"
#include "lepton_cci.h"
#include "stp_link.h"

#include <string.h>

static void begin_response(const wire_message_t *request, uint8_t local_address,
                           wire_message_t *response) {
  memset(response, 0, sizeof(*response));
  response->kind = WIRE_KIND_RESPONSE;
  response->source = local_address;
  response->destination = request->source;
  response->sequence = request->sequence;
  response->opcode = request->opcode;
}

static void set_result(wire_message_t *response, int16_t result) {
  wire_put_u16(response->payload, (uint16_t)result);
  response->payload_length = 2U;
  if (result != COMMAND_OK) {
    response->flags |= WIRE_FLAG_ERROR;
  }
}

static bool append_u32(wire_message_t *response, uint32_t value) {
  if (response->payload_length + 4U > WIRE_MAX_PAYLOAD) {
    return false;
  }
  wire_put_u32(&response->payload[response->payload_length], value);
  response->payload_length += 4U;
  return true;
}

void command_dispatch_init(void) {
}

static void get_info(wire_message_t *response) {
  set_result(response, COMMAND_OK);
  response->payload[response->payload_length++] = APP_FW_VERSION_MAJOR;
  response->payload[response->payload_length++] = APP_FW_VERSION_MINOR;
  response->payload[response->payload_length++] = APP_FW_VERSION_PATCH;
  response->payload[response->payload_length++] = WIRE_VERSION;
  for (uint8_t i = 0U; i < 3U; ++i) {
    append_u32(response, board_unique_id_word(i));
  }
  /* Capabilities: Lepton, UVC, CDC, dosimeter, multidrop. */
  append_u32(response, 0x0000001FU);
}

static void get_health(wire_message_t *response) {
  set_result(response, COMMAND_OK);
  const uint32_t *values = (const uint32_t *)&g_health;
  for (size_t i = 0U; i < sizeof(g_health) / sizeof(uint32_t); ++i) {
    if (!append_u32(response, values[i])) {
      set_result(response, COMMAND_INTERNAL_ERROR);
      return;
    }
  }
}

static void get_lepton_status(wire_message_t *response) {
  lepton_capture_status_t status;
  lepton_capture_get_status(&status);
  set_result(response, COMMAND_OK);
  response->payload[response->payload_length++] = (uint8_t)status.state;
  response->payload[response->payload_length++] = 0U;
  wire_put_u16(&response->payload[response->payload_length],
               (uint16_t)status.last_cci_result);
  response->payload_length += 2U;
  wire_put_u16(&response->payload[response->payload_length],
               (uint16_t)status.last_ffc_result);
  response->payload_length += 2U;
  wire_put_u16(&response->payload[response->payload_length], 0U);
  response->payload_length += 2U;
  append_u32(response, status.frame_generation);
  append_u32(response, status.last_vsync_ms);
}

static void get_ffc_status(wire_message_t *response) {
  lepton_ffc_status_t ffc;
  int result = lepton_cci_get_ffc_status(&ffc);
  if (result != LEPTON_CCI_OK) {
    set_result(response, (int16_t)result);
    return;
  }
  set_result(response, COMMAND_OK);
  append_u32(response, ffc.shutter_mode);
  append_u32(response, ffc.temp_lockout_state);
  append_u32(response, ffc.elapsed_since_ffc_ms);
  append_u32(response, ffc.desired_period_ms);
  append_u32(response, (uint32_t)ffc.desired_temp_delta);
  append_u32(response, (uint32_t)(int32_t)ffc.ffc_state);
}

static void cci_get(const wire_message_t *request, wire_message_t *response) {
  if (request->payload_length != 4U) {
    set_result(response, COMMAND_BAD_LENGTH);
    return;
  }
  uint16_t command = wire_get_u16(&request->payload[0]);
  uint16_t count = wire_get_u16(&request->payload[2]);
  if ((count == 0U) || (count > (WIRE_MAX_PAYLOAD - 4U) / 2U)) {
    set_result(response, COMMAND_BAD_LENGTH);
    return;
  }
  uint16_t words[(WIRE_MAX_PAYLOAD - 4U) / 2U];
  int result = lepton_cci_get(command, words, &count);
  set_result(response, (int16_t)result);
  if (result != LEPTON_CCI_OK) {
    return;
  }
  wire_put_u16(&response->payload[response->payload_length], count);
  response->payload_length += 2U;
  for (uint16_t i = 0U; i < count; ++i) {
    wire_put_u16(&response->payload[response->payload_length], words[i]);
    response->payload_length += 2U;
  }
}

static void cci_set(const wire_message_t *request, wire_message_t *response) {
  if (request->payload_length < 6U) {
    set_result(response, COMMAND_BAD_LENGTH);
    return;
  }
  uint16_t command = wire_get_u16(&request->payload[0]);
  uint16_t count = wire_get_u16(&request->payload[2]);
  if ((count == 0U) || (request->payload_length != 4U + count * 2U)) {
    set_result(response, COMMAND_BAD_LENGTH);
    return;
  }
  uint16_t words[LEPTON_CCI_MAX_WORDS];
  if (count > LEPTON_CCI_MAX_WORDS) {
    set_result(response, COMMAND_BAD_LENGTH);
    return;
  }
  for (uint16_t i = 0U; i < count; ++i) {
    words[i] = wire_get_u16(&request->payload[4U + i * 2U]);
  }
  set_result(response, (int16_t)lepton_cci_set(command, words, count));
}

static void cci_run(const wire_message_t *request, wire_message_t *response) {
  if (request->payload_length != 2U) {
    set_result(response, COMMAND_BAD_LENGTH);
    return;
  }
  set_result(response, (int16_t)lepton_cci_run(wire_get_u16(request->payload)));
}

static void register_read(const wire_message_t *request, wire_message_t *response) {
  if (request->payload_length != 2U) {
    set_result(response, COMMAND_BAD_LENGTH);
    return;
  }
  uint16_t value = 0U;
  int result = lepton_cci_read_register(wire_get_u16(request->payload), &value);
  set_result(response, (int16_t)result);
  if (result == LEPTON_CCI_OK) {
    wire_put_u16(&response->payload[response->payload_length], value);
    response->payload_length += 2U;
  }
}

static void register_write(const wire_message_t *request, wire_message_t *response) {
  if (request->payload_length != 4U) {
    set_result(response, COMMAND_BAD_LENGTH);
    return;
  }
  set_result(response, (int16_t)lepton_cci_write_register(
      wire_get_u16(&request->payload[0]), wire_get_u16(&request->payload[2])));
}

static bool assign_bus_address(const wire_message_t *request, wire_message_t *response) {
  if (request->payload_length != 13U) {
    set_result(response, COMMAND_BAD_LENGTH);
    return true;
  }
  for (uint8_t i = 0U; i < 3U; ++i) {
    if (wire_get_u32(&request->payload[i * 4U]) != board_unique_id_word(i)) {
      return false;
    }
  }
  uint8_t address = request->payload[12];
  set_result(response, stp_link_set_target_id(address) ? COMMAND_OK : COMMAND_BAD_LENGTH);
  if ((response->flags & WIRE_FLAG_ERROR) == 0U) {
    response->payload[response->payload_length++] = address;
  }
  return true;
}

static void stream_status(wire_message_t *response) {
  uint32_t generation = 0U;
  const uint16_t *frame = lepton_capture_latest_frame(&generation);
  set_result(response, frame == NULL ? COMMAND_NOT_READY : COMMAND_OK);
  append_u32(response, generation);
  append_u32(response, APP_FRAME_BYTES);
}

static void frame_chunk(const wire_message_t *request, wire_message_t *response) {
  if (request->payload_length != 10U) {
    set_result(response, COMMAND_BAD_LENGTH);
    return;
  }
  uint32_t requested_generation = wire_get_u32(&request->payload[0]);
  uint32_t offset = wire_get_u32(&request->payload[4]);
  uint16_t requested = wire_get_u16(&request->payload[8]);
  if (offset == 0U) {
    /* Pin the image before reading the first chunk; at the full frame rate the
     * published buffer is replaced several times during a chunked transfer. */
    lepton_capture_hold_frame(true);
  }
  uint32_t generation = 0U;
  const uint16_t *frame = lepton_capture_latest_frame(&generation);
  if (frame == NULL) {
    lepton_capture_hold_frame(false);
    set_result(response, COMMAND_NOT_READY);
    return;
  }
  if ((requested_generation != 0U) && (requested_generation != generation)) {
    lepton_capture_hold_frame(false);
    set_result(response, COMMAND_STALE_FRAME);
    append_u32(response, generation);
    return;
  }
  if ((offset >= APP_FRAME_BYTES) || (requested == 0U)) {
    set_result(response, COMMAND_BAD_LENGTH);
    return;
  }
  uint16_t maximum = (uint16_t)(WIRE_MAX_PAYLOAD - 14U);
  uint32_t remaining = APP_FRAME_BYTES - offset;
  uint16_t length = requested < maximum ? requested : maximum;
  if (length > remaining) {
    length = (uint16_t)remaining;
  }

  set_result(response, COMMAND_OK);
  append_u32(response, generation);
  append_u32(response, APP_FRAME_BYTES);
  append_u32(response, offset);
  memcpy(&response->payload[response->payload_length], ((const uint8_t *)frame) + offset, length);
  response->payload_length += length;
  if (offset + length < APP_FRAME_BYTES) {
    response->flags |= WIRE_FLAG_MORE;
  } else {
    lepton_capture_hold_frame(false);
  }
}

static void dosimeter_status(wire_message_t *response) {
  dosimeter_snapshot_t sample;
  dosimeter_get_snapshot(&sample);
  set_result(response, COMMAND_OK);
  append_u32(response, sample.timestamp_ms);
  wire_put_u16(&response->payload[response->payload_length], sample.raw_mean);
  response->payload_length += 2U;
  wire_put_u16(&response->payload[response->payload_length], sample.raw_min);
  response->payload_length += 2U;
  wire_put_u16(&response->payload[response->payload_length], sample.raw_max);
  response->payload_length += 2U;
  wire_put_u16(&response->payload[response->payload_length], sample.raw_stddev);
  response->payload_length += 2U;
  append_u32(response, sample.vdda_mv);
  append_u32(response, sample.voltage_uv);
  append_u32(response, sample.filtered_voltage_uv);
  append_u32(response, (uint32_t)sample.zero_voltage_uv);
  append_u32(response, (uint32_t)sample.dose_microrad);
  append_u32(response, sample.flags);
  /* Saved-state fields let a host confirm a zero reached flash. */
  append_u32(response, (uint32_t)(int32_t)settings_status());
  append_u32(response, settings_save_count());
}

static void handle_dosimeter_zero(wire_message_t *response) {
  set_result(response, dosimeter_begin_zero() ? COMMAND_OK : COMMAND_NOT_READY);
  /* The averaging window, so a host knows how long to wait. */
  append_u32(response, APP_DOSIMETER_ZERO_SAMPLES);
}

static void handle_dosimeter_set_zero(const wire_message_t *request,
                                      wire_message_t *response) {
  if (request->payload_length != 4U) {
    set_result(response, COMMAND_BAD_LENGTH);
    return;
  }
  dosimeter_set_zero((int32_t)wire_get_u32(&request->payload[0]));
  set_result(response, COMMAND_OK);
}

bool command_dispatch(const wire_message_t *request, uint8_t local_address,
                      wire_message_t *response) {
  if ((request == NULL) || (response == NULL) || (request->kind != WIRE_KIND_REQUEST)) {
    return false;
  }
  begin_response(request, local_address, response);
  switch (request->opcode) {
    case OPCODE_GET_INFO:
    case OPCODE_DISCOVER:
      get_info(response);
      break;
    case OPCODE_GET_HEALTH:
      get_health(response);
      break;
    case OPCODE_LEPTON_STATUS:
      get_lepton_status(response);
      break;
    case OPCODE_LEPTON_CCI_GET:
      cci_get(request, response);
      break;
    case OPCODE_LEPTON_CCI_SET:
      cci_set(request, response);
      break;
    case OPCODE_LEPTON_FFC_STATUS:
      get_ffc_status(response);
      break;
    case OPCODE_LEPTON_RUN_FFC:
      set_result(response, (int16_t)lepton_capture_run_ffc());
      break;
    case OPCODE_LEPTON_CCI_RUN:
      cci_run(request, response);
      break;
    case OPCODE_LEPTON_REG_READ:
      register_read(request, response);
      break;
    case OPCODE_LEPTON_REG_WRITE:
      register_write(request, response);
      break;
    case OPCODE_STREAM_STATUS:
      stream_status(response);
      break;
    case OPCODE_FRAME_CHUNK:
      frame_chunk(request, response);
      break;
    case OPCODE_DOSIMETER_STATUS:
      dosimeter_status(response);
      break;
    case OPCODE_DOSIMETER_ZERO:
      handle_dosimeter_zero(response);
      break;
    case OPCODE_DOSIMETER_SET_ZERO:
      handle_dosimeter_set_zero(request, response);
      break;
    case OPCODE_BUS_STATUS: {
      stp_link_status_t link;
      stp_link_get_status(&link);
      set_result(response, COMMAND_OK);
      append_u32(response, APP_RS485_BAUD);
      response->payload[response->payload_length++] = link.target_id;
      response->payload[response->payload_length++] = link.hrt_enabled ? 1U : 0U;
      append_u32(response, link.commands_received);
      append_u32(response, link.lrt_requests);
      append_u32(response, link.lrt_sent);
      append_u32(response, link.hrt_sent);
      append_u32(response, link.rejected_target);
      append_u32(response, link.crc_errors);
      append_u32(response, link.type_errors);
      append_u32(response, link.coarse_time);
      break;
    }
    case OPCODE_BUS_ASSIGN_ADDRESS:
      return assign_bus_address(request, response);
    default:
      set_result(response, COMMAND_BAD_OPCODE);
      break;
  }
  return true;
}
