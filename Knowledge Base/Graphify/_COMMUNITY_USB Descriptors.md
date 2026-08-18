---
type: community
cohesion: 0.36
members: 10
---

# USB Descriptors

**Cohesion:** 0.36 - loosely connected
**Members:** 10 nodes

## Members
- [[USBD_SpeedTypeDef]] - code - src/usb/usbd_desc.c
- [[config_descriptor()]] - code - src/usb/usbd_desc.c
- [[device_descriptor()]] - code - src/usb/usbd_desc.c
- [[interface_descriptor()]] - code - src/usb/usbd_desc.c
- [[lang_descriptor()]] - code - src/usb/usbd_desc.c
- [[manufacturer_descriptor()]] - code - src/usb/usbd_desc.c
- [[product_descriptor()]] - code - src/usb/usbd_desc.c
- [[serial_descriptor()]] - code - src/usb/usbd_desc.c
- [[unicode_hex()]] - code - src/usb/usbd_desc.c
- [[usbd_desc.c]] - code - src/usb/usbd_desc.c

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/USB_Descriptors
SORT file.name ASC
```

## Connections to other communities
- 1 edge to [[_COMMUNITY_Command Dispatcher]]

## Top bridge nodes
- [[serial_descriptor()]] - degree 4, connects to 1 community