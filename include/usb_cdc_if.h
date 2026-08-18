#pragma once

#include "usbd_cdc.h"

extern USBD_CDC_ItfTypeDef g_usb_cdc_interface;
void usb_cdc_task(void);

