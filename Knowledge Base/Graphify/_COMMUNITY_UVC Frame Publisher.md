---
type: community
cohesion: 0.40
members: 5
---

# UVC Frame Publisher

**Cohesion:** 0.40 - moderately connected
**Members:** 5 nodes

## Members
- [[usb_video_if.c]] - code - src/usb/usb_video_if.c
- [[video_control()]] - code - src/usb/usb_video_if.c
- [[video_data()]] - code - src/usb/usb_video_if.c
- [[video_deinit()]] - code - src/usb/usb_video_if.c
- [[video_init()]] - code - src/usb/usb_video_if.c

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/UVC_Frame_Publisher
SORT file.name ASC
```

## Connections to other communities
- 1 edge to [[_COMMUNITY_Command Dispatcher]]

## Top bridge nodes
- [[video_data()]] - degree 2, connects to 1 community