#pragma once

#include <stdbool.h>

/* The data tap starts closed and stays closed across ownership changes. Only
 * GO received while this node owns the bus opens it; STOP closes it.
 * Ownership and capture state independently guard transmission. */
typedef struct {
  bool open;
} hrt_gate_t;

void hrt_gate_init(hrt_gate_t *gate);
void hrt_gate_go(hrt_gate_t *gate);
void hrt_gate_stop(hrt_gate_t *gate);
bool hrt_gate_can_send(const hrt_gate_t *gate, bool selected,
                       bool capture_ready, bool image_ready);
