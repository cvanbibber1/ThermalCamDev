---
type: interface-spec
status: review
transport: USB Full-Speed
---

# USB interface

## Device composition

The implemented baseline is a composite device:

1. **UVC function** for thermal video.
2. **CDC ACM function** for the shared binary command, event, and low-rate telemetry
   protocol during bring-up.

CDC is chosen for initial host-tool simplicity. A UVC Extension Unit or WinUSB vendor
interface may be added later, but must map to the same internal command dispatcher.

The CubeF4 composite builder requires VIDEO to be registered first because its UVC
descriptor contains fixed references to interfaces 0 and 1. CDC therefore owns interfaces
2 and 3. Interface Association Descriptors are enabled. The endpoint allocation is EP1 IN
isochronous for UVC, EP2 IN/OUT bulk for CDC data, and EP3 IN interrupt for CDC
notification, plus EP0. The 320-word OTG FS FIFO RAM allocation is RX 96, EP0 TX 32,
EP1 TX 128, EP2 TX 48, and EP3 TX 16; enumeration and sustained transfer remain hardware
acceptance tests.

## UVC video mode

Baseline stream:

- 160 x 120 pixels.
- 16 bits per pixel radiometric/TLinear grayscale.
- One complete valid Lepton frame per UVC frame.
- Nominal unique frame rate reported as the measured Lepton rate near 8.6 Hz.
- A presentation timestamp derived from a monotonic MCU timer is planned after initial
  host interoperability testing; the baseline UVC payload header carries frame ID only.

Payload is about 38,400 bytes per frame and about 330 kB/s at 8.6 fps, which is within
USB Full-Speed bandwidth. Use isochronous transfers and a payload size selected after
host compatibility testing. UVC frame-based/vendor GUID Y16 interoperability must be
tested on Windows and Linux; if common viewers reject Y16, add a secondary YUY2/false-
color mode without removing the radiometric mode.

UVC backpressure never blocks capture. If the host misses its slot or buffers fill, drop
the oldest unpublished frame, increment a counter, and mark discontinuity in the next
payload header.

## Command and telemetry channel

The CDC byte stream uses exactly the same addressed message schema and operation codes as
the field bus; address zero identifies the USB-local camera endpoint. It uses COBS framing
with a zero delimiter and CRC-32C. Maximum decoded command payload is 2048 bytes; frame
data is normally carried by UVC, but bounded diagnostic snapshot chunks are also available
over CDC.

Telemetry defaults to 1 Hz and includes firmware/camera health, dosimeter readings,
temperatures, stream epoch, frame counters, and transport errors.

## USB hardware constraints

- USB VBUS is not connected. Configure the OTG FS peripheral for no-VBUS-sense,
  self-powered operation and retain PA9 as Lepton `RESET_L`.
- The 48 MHz USB clock is PLLI2SQ-derived from the MAX7375 HSE. Hardware acceptance
  requires reliable enumeration and transfer testing; production clock compliance must
  still be demonstrated across component, voltage, and temperature tolerance.
- A production USB VID/PID and string/serial-number policy are required before release.
