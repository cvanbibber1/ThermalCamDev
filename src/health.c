#include "health.h"

#include <limits.h>

health_counters_t g_health;

void health_increment(uint32_t *counter) {
  if ((counter != 0) && (*counter != UINT32_MAX)) {
    ++(*counter);
  }
}

