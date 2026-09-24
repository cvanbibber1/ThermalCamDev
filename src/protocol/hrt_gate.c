#include "protocol/hrt_gate.h"

void hrt_gate_init(hrt_gate_t *gate) {
  gate->open = false;
}

void hrt_gate_go(hrt_gate_t *gate) {
  gate->open = true;
}

void hrt_gate_stop(hrt_gate_t *gate) {
  gate->open = false;
}

bool hrt_gate_can_send(const hrt_gate_t *gate, bool selected,
                       bool capture_ready, bool image_ready) {
  return gate->open && selected && capture_ready && image_ready;
}
