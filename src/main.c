#include "board.h"
#include "command_dispatch.h"
#include "dosimeter.h"
#include "health.h"
#include "lepton_capture.h"
#include "rs485.h"
#include "usb_device.h"

int main(void) {
  HAL_Init();
  if (!board_init()) {
    board_fatal(g_health.fatal_code == 0U ? 0xB001U : g_health.fatal_code);
  }
  g_health.reset_cause = board_reset_cause();

  command_dispatch_init();
  if (!dosimeter_init()) {
    board_fatal(0xA001U);
  }
  lepton_capture_init();
  if (!usb_device_init()) {
    board_fatal(0xA002U);
  }
  if (!rs485_init()) {
    board_fatal(0xA003U);
  }

  for (;;) {
    lepton_capture_task();
    dosimeter_task();
    usb_device_task();
    rs485_task();
  }
}
