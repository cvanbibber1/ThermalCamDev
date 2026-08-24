#include "board.h"
#include "command_dispatch.h"
#include "dosimeter.h"
#include "health.h"
#include "lepton_capture.h"
#include "stp_link.h"
#include "settings.h"
#include "usb_device.h"

int main(void) {
  HAL_Init();
  if (!board_init()) {
    board_fatal(g_health.fatal_code == 0U ? 0xB001U : g_health.fatal_code);
  }
  g_health.reset_cause = board_reset_cause();
  g_health.previous_fatal_code = board_previous_fatal();
  /* Started as early as the clocks allow, so a fault during the rest of
   * bring-up is recoverable rather than permanent. */
  board_watchdog_init();

  /* Before the dosimeter, which takes its zero reference from here. */
  settings_init();
  command_dispatch_init();
  /* The dosimeter and USB are not required to fly. Losing either costs a
   * measurement or a bench convenience; refusing to start would cost the
   * mission, and in orbit there is no one to power cycle the board. The
   * failure is recorded and the camera carries on. */
  if (!dosimeter_init()) {
    g_health.fatal_code = 0xA001U;
  }
  lepton_capture_init();
  if (!usb_device_init()) {
    g_health.fatal_code = 0xA002U;
  }
  /* USART2 speaks the STP/DICE packet protocol on this branch, not the
   * development COBS transport. */
  /* The link is the only way to reach the experiment once it is flying, so
   * this one really is fatal: the watchdog will reset and try again. */
  if (!stp_link_init()) {
    board_fatal(0xA003U);
  }

  for (;;) {
    board_watchdog_refresh();
    lepton_capture_task();
    dosimeter_task();
    usb_device_task();
    stp_link_task();
    /* Last: a save stalls the core for the sector erase, so let the transports
     * finish delivering the response that requested it first. */
    settings_task();
  }
}
