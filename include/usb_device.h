#pragma once

#include <stdbool.h>
#include <stdint.h>

bool usb_device_init(void);
void usb_device_task(void);
bool usb_device_configured(void);

