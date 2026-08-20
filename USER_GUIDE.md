# Thermal Camera User Guide

A practical guide to running the Lepton 3.1R radiometric thermal camera, over
USB on the bench and over RS-422 for flight. No prior knowledge of the design
is assumed.

- [What the camera is](#what-the-camera-is)
- [One-time setup](#one-time-setup)
- [Start the graphical interface](#start-the-graphical-interface)
- [Start the command line interface](#start-the-command-line-interface)
- [Using RS-422](#using-rs-422)
- [Everyday tasks](#everyday-tasks)
- [Understanding the readings](#understanding-the-readings)
- [When something looks wrong](#when-something-looks-wrong)
- [Command reference](#command-reference)
- [Response reference](#response-reference)

---

## What the camera is

A 160 x 120 thermal camera that reports **absolute temperature for every
pixel**, plus a radiation dosimeter. It presents three interfaces:

| Interface | Carries | Used by |
|---|---|---|
| USB video | Live images, every pixel an absolute temperature | The graphical interface |
| USB serial | Commands and telemetry | Both interfaces |
| RS-422 | Commands, vitals and images for flight | The flight computer, or `stp_monitor.py` |

Two things are worth knowing before you start.

**Pixels are temperatures, not brightness.** Each pixel is a temperature in
hundredths of a kelvin. Divide by 100 for kelvin, then subtract 273.15 for
Celsius. The colours you see are a display choice applied on the host; the
underlying numbers never change.

**The camera pauses about every three minutes.** It closes an internal shutter
to recalibrate, which takes roughly a second and briefly freezes the image.
This is normal and required for accuracy. See
[flat-field correction](#flat-field-correction).

---

## One-time setup

You need Python 3.11 or newer. From the project folder:

```powershell
python -m pip install -r requirements-dev.txt
```

Then plug the camera into USB. Windows installs it automatically; it appears as
a camera and a serial port. Confirm it is there:

```powershell
python .\tools\thermalcam_cli.py --port COM55 info
```

Replace `COM55` with your port. To find it, look for the port whose hardware ID
contains `1209:F412`:

```powershell
python -c "import serial.tools.list_ports as p; [print(x.device, x.hwid) for x in p.comports()]"
```

A healthy reply looks like:

```
firmware=0.2.0 uid=22800C000651353532343332 capabilities=0x0000001F
```

If that works, everything else will.

---

## Start the graphical interface

```powershell
python .\tools\thermalcam_app.py
```

That is the whole command. It finds the camera and the serial port by itself.
If you have several cameras, or want a specific port:

```powershell
python .\tools\thermalcam_app.py --index 1 --port COM55 --output captures
```

| Option | Meaning |
|---|---|
| `--index` | Which video device to use. Omit to search. |
| `--port` | Serial port for control. Omit to search. |
| `--output` | Where images and video are written. Default `captures`. |

### The window

The image is on the left, controls on the right, status along the bottom.

- **Click anywhere on the image** to drop a marker showing that spot's
  temperature. The hottest and coldest points are marked automatically. Switch
  between Celsius and Fahrenheit with the radio buttons.
- **Palette** changes the colour scheme. **Contrast** changes how the
  temperature range is mapped to those colours: `plateau` suits most scenes,
  `manual` lets you fix the range in degrees, which is what you want when
  comparing images.
- **Save image** writes a picture, the raw data, per-pixel temperatures as a
  spreadsheet file, and the dose at that moment.
- **Start recording** writes video plus a companion file holding the dose for
  the whole clip.
- **Command console** runs any command from the
  [command reference](#command-reference) and shows the reply. This exists
  because a serial port can only be held by one program: while the window is
  open the command line tool cannot reach the camera, so the console gives you
  the same commands without closing anything.

The bottom bar shows the temperature range, the frame rate, whether video is
`live`, the serial port, and when the camera last recalibrated.

---

## Start the command line interface

Use this when you want to script something, or when the window is closed.

```powershell
python .\tools\thermalcam_cli.py --port COM55 <command>
```

Every command in the [reference](#command-reference) works this way. The
options before the command are:

| Option | Default | Meaning |
|---|---|---|
| `--port` | required | Serial port |
| `--baud` | 921600 | Ignored over USB; used on RS-422 |
| `--address` | 1 | Which camera to talk to |
| `--timeout` | 2.0 | Seconds to wait for a reply |

Examples:

```powershell
python .\tools\thermalcam_cli.py --port COM55 health
python .\tools\thermalcam_cli.py --port COM55 dosimeter
python .\tools\thermalcam_cli.py --port COM55 frame --output shot.raw
```

Turn a saved frame into a picture:

```powershell
python .\tools\render_frame.py shot.raw -o shot.png --destripe --colormap ironbow
```

Capture from the video interface without the window:

```powershell
python .\tools\uvc_capture.py --seconds 30 --output last.raw --save-all frames\
```

> **Only one program at a time.** The serial port and the video interface are
> each exclusive. Close the window before using these tools, or use the
> window's command console instead. If a tool reports the camera is busy, a
> previous run is probably still open.

---

## Using RS-422

This is the flight path. It needs the `flight-test/rs422` firmware, which sends
the same telemetry over both USB and RS-422 so you can compare them.

### Wiring

The camera's transceiver connects to your RS-422 to USB converter. The
converter appears as another serial port, separate from the camera's own.

> **Before connecting to a bus with other equipment on it**, read the caution
> in `Knowledge Base/Project/Flight Test RS422.md`. The transmit-enable line
> currently has a pull-up fitted, so this node may drive the bus when it should
> be listening. Point to point with a converter is fine.

### Watch the traffic

```powershell
python .\tools\stp_monitor.py --port COM7
```

Use your converter's port, not the camera's. Vitals arrive once a second:

```
LRT  up=  123.4s  streaming  gen=1084    dose=-61.397 rad  scene 18.2..27.6 C
```

Useful options:

| Option | Meaning |
|---|---|
| `--save-frames DIR` | Reassemble images from the high-rate stream and write them |
| `--request lrt` | Ask for vitals instead of waiting |
| `--request hrt-go` | Start the image stream |
| `--target N` | Which camera to listen for. Default `0xC7` |
| `--raw` | Show every packet |
| `--little-endian`, `--crc-seed` | Diagnostics; see below |

### Link settings

These are fixed and both ends already agree. They are listed so you can set up
a third-party analyser, or check one if something looks wrong.

| Setting | Value |
|---|---|
| Baud | 921600, 8 data bits, no parity, 1 stop bit |
| Byte order | Big endian. The sync word `0x1ACFFC1D` goes out as `1A CF FC 1D` |
| Checksum | CRC-16/CCITT-FALSE: polynomial `0x1021`, seed `0xFFFF`, no reflection, no final xor |
| Checksum position | The **last two bytes of every packet**, in both directions |
| Checksum coverage | Everything after the four sync bytes, up to but not including the checksum |
| This camera's Target ID | `0xC7`, 199 decimal |

If nothing decodes, check the wiring and the baud rate first. The
`--little-endian` and `--crc-seed` options exist only so a mismatch can be
proved from the ground without rebuilding firmware; they should not be needed.
These settings live in `include/protocol/stp_protocol.h` if they ever change.

### The two streams

The link carries two separate things, and they do not overlap.

| Name | Purpose | Rate |
|---|---|---|
| LRT, low rate | **Vitals only.** Uptime, camera state, frame counter, dosimeter reading, scene temperature summary, and every error counter. No image data. | Once a second, or on request |
| HRT, high rate | **The image, and nothing else.** One frame split across 31 packets, each carrying a header so it can be reassembled. | Continuous while enabled |

Vitals are always given the transmitter first, so image traffic cannot crowd
them out. They cost about 1.4% of the link.

### Image rate over RS-422

Measured on the bench: **61 image packets a second, which is 1.98 frames a
second**, using 87% of the link. The camera itself produces 8.8 frames a
second, so RS-422 carries roughly one frame in four.

This is a limit of the link, not something left untuned:

```
921600 baud, 8N1     = 92,160 bytes/s
one image packet     =  1,288 bytes = 14.0 ms
one frame            =     31 packets = 39,928 bytes
absolute ceiling     =    2.3 frames/s
```

The cost is that every pixel is a full 16-bit temperature. If a smoother
picture matters more than per-pixel temperature, sending an 8-bit image would
roughly double the rate; that has not been done because it would remove the
radiometry. For a genuinely live picture use the USB video interface, which
runs at the camera's full 8.8 frames a second.

---

## Everyday tasks

### Read a temperature

In the window, click the spot. From the command line, save a frame and inspect
it:

```powershell
python .\tools\thermalcam_cli.py --port COM55 frame --output shot.raw
python -c "import sys; sys.path.insert(0,'tools'); import thermal_imaging as t; f=t.load_raw('shot.raw'); print(t.format_temperature(f[60,80],'C'))"
```

### Flat-field correction

The camera recalibrates itself roughly every three minutes, or sooner if its
temperature drifts. You do not normally need to do anything.

Force one if the image looks smeared or drifted after a temperature change:

```powershell
python .\tools\thermalcam_cli.py --port COM55 ffc
```

or press **Run flat-field correction** in the window. The image freezes for
about a second. Check the policy with `ffc-status`.

### Zero the dosimeter

The dose reading is derived from a voltage:

```
volts at PA4 = 0.1575 + 0.0025 x dose in rad
```

By default the camera uses that nominal 157.5 mV intercept. To measure the
intercept for your particular unit, remove any radiation source and:

```powershell
python .\tools\thermalcam_cli.py --port COM55 dosimeter-zero
```

or press **Zero** in the window. It averages for about two seconds and stores
the result in the camera's flash, so it survives power cycles. To go back to
the nominal value:

```powershell
python .\tools\thermalcam_cli.py --port COM55 dosimeter-set-zero 0
```

### Check the camera is healthy

```powershell
python .\tools\thermalcam_cli.py --port COM55 health
```

The counters that matter are described in the
[response reference](#response-reference).

---

## Understanding the readings

**Temperatures.** Accurate for comparing objects in a scene. Absolute accuracy
depends on the emissivity of what you are looking at; shiny metal reads far
colder than it is. Let the camera warm up for a few minutes and allow it to
recalibrate before trusting absolute values.

**Dose.** Expect a large negative number, around -61 rad, on current hardware.
That is not a fault. The detector input is not yet driven, so it sits near
4 mV where zero dose should be 157.5 mV, and the equation reports the
difference honestly. Once the input is driven, either the nominal intercept
will be right or you zero the unit. Until then the reading also wanders by
about 0.1 rad because the signal sits at the very bottom of the converter's
range.

**Frame rate.** About 8.8 new images a second. The video interface delivers
about 13 per second, repeating some, which is normal.

---

## When something looks wrong

| Symptom | Cause and fix |
|---|---|
| `could not open port ... Access is denied` | Another program holds the port. Close the window, or another command line tool. |
| `No Y16 camera found` | Another program is streaming video, or the camera is unplugged. Look for a leftover Python process. |
| Window shows `no image for Ns` | Video stalled. It restarts itself; after a firmware load it may need the window restarted. |
| `camera returned error -3` | Not ready. Usually the camera is still starting; it takes about ten seconds. |
| `camera returned error -4` | The image changed during transfer. Just ask again. |
| Image drifted or smeared | Run a flat-field correction. |
| Dose reads about -61 rad | Expected on current hardware; see above. |
| Nothing decodes on RS-422 | Check the wiring and baud rate, then the [link settings](#link-settings). |

---

## Command reference

Every command works in three places: on the command line as
`thermalcam_cli.py --port <port> <command>`, in the window's command console as
just `<command>`, and over RS-422 to a camera by address. The GUI has buttons
for the common ones; the console covers the rest.

### Camera information

| Command | Arguments | What it does |
|---|---|---|
| `info` | none | Firmware version, unique chip ID, and which features are built in. Use it to confirm the link works. |
| `health` | none | All error and activity counters since power on. |
| `discover` | none | Same as `info`, but broadcast, so every camera on a bus replies. Used to find cameras whose address you do not know. |

### Imaging

| Command | Arguments | What it does |
|---|---|---|
| `lepton-status` | none | What the imaging sensor is doing, how many images it has produced, and whether start-up succeeded. |
| `stream-status` | none | The current image number and its size in bytes. |
| `frame` | `--output FILE` | Downloads the current image as raw data. Slow over serial, about 2 per second; the video interface is the fast path. |

### Flat-field correction

| Command | Arguments | What it does |
|---|---|---|
| `ffc` | none | Recalibrates now. Freezes the image for about a second. |
| `ffc-status` | none | Whether recalibration is automatic, how long since the last one, and the interval and temperature change that trigger it. |

### Dosimeter

| Command | Arguments | What it does |
|---|---|---|
| `dosimeter` | none | Full reading: raw counts, voltages, the intercept in use, dose in rad, and status flags. |
| `dosimeter-zero` | none | Measures the intercept for this unit by averaging for about two seconds, and stores it permanently. Do this with no radiation source present. |
| `dosimeter-set-zero` | `MICROVOLTS` | Sets the intercept directly. Pass `0` to return to the nominal 157.5 mV. |

### Sensor internals

These reach into the imaging sensor directly. You will not need them for normal
use, and a wrong value can stop the camera imaging until it is restarted.

| Command | Arguments | What it does |
|---|---|---|
| `cci-get` | `COMMAND_ID WORDS` | Reads a sensor setting. Example: `cci-get 0x4EC0 2` reads whether radiometry is enabled. |
| `cci-set` | `COMMAND_ID VALUES...` | Writes a sensor setting. |
| `cci-run` | `COMMAND_ID` | Runs a sensor operation that takes no data. |
| `reg-read` | `REGISTER` | Reads one hardware register. |
| `reg-write` | `REGISTER VALUE` | Writes one hardware register. |

> Do not write the flat-field correction settings object, command `0x023C`.
> Writing it is accepted but only partly applied, which silently disables
> automatic recalibration. Read it if you want to check the policy.

### RS-422 bus

| Command | Arguments | What it does |
|---|---|---|
| `bus-status` | none | Link speed, this camera's ID, whether the image stream is on, and packet counters. |
| `assign` | `UID ADDRESS` | Gives a camera a new Target ID, matched by its unique chip ID from `info`. Stored permanently and survives power cycles. This camera is assigned `0xC7`; use this to give each camera on a chain its own ID. Example: `assign 22800C000651353532343332 0xC7`. |

---

## Response reference

### `info` and `discover`

```
firmware=0.2.0 uid=22800C000651353532343332 capabilities=0x0000001F
```

| Field | Meaning |
|---|---|
| `firmware` | Firmware version |
| `uid` | Permanent unique ID of this camera, used by `assign` |
| `capabilities` | Bit flags for the features built in |

### `lepton-status`

```
state=streaming cci_result=0 ffc_result=0 generation=1084 last_vsync=349843ms
```

| Field | Meaning |
|---|---|
| `state` | `streaming` is healthy. `booting` and `configuring` are normal for the first ten seconds. `resync` means it is re-establishing the image link, briefly normal. `retry` means start-up failed and it is trying again. |
| `cci_result` | 0 if the sensor was configured successfully |
| `ffc_result` | 0 if the recalibration policy was applied |
| `generation` | How many complete images have been produced. Should climb steadily. |
| `last_vsync` | When the sensor last signalled a new image |

### `dosimeter`

```
t=12352ms adc=6 min=0 max=26 sd=6
vdda=2992mV voltage=4383uV filtered=7591uV zero=157500uV
dose=-59.9636 rad  flags=nominal-calibration  settings=0 saves=0
```

| Field | Meaning |
|---|---|
| `adc`, `min`, `max`, `sd` | Raw converter counts, out of 4095, and their spread |
| `vdda` | Supply voltage measured internally. About 2990 mV is normal. |
| `voltage` | Instantaneous input voltage |
| `filtered` | Smoothed input voltage, with a two second time constant. This is what dose is calculated from. |
| `zero` | The intercept in use: 157500 uV nominal, or a value measured for this unit |
| `dose` | Dose in rad, from `(filtered - zero) / 2500`. Negative means the input is below its zero point. |
| `settings` | 0 stored, 1 using defaults, 2 saving, negative means a save failed |
| `saves` | How many times settings have been written to flash |

Flags:

| Flag | Meaning |
|---|---|
| `nominal-calibration` | Using the standard 157.5 mV intercept, not one measured for this unit |
| `saturated` | Input is at the top of the range; the reading is not trustworthy |
| `stale` | No fresh sample recently |
| `zeroing` | A zero measurement is in progress; wait about two seconds |
| `none` | A measured intercept is in use and the reading is current |

### `ffc-status`

```
shutter_mode=auto lockout=0 ffc_state=0
elapsed=100.7s period=180s temp_delta=1.50C
```

`shutter_mode` should be `auto`. `elapsed` counts up to `period`, then the
camera recalibrates and it resets. It also recalibrates early if its
temperature moves by `temp_delta`. `lockout` is non-zero when the camera is too
hot or cold to recalibrate safely.

### `health`

All counters since power on. The ones worth watching:

| Counter | Healthy value | Meaning if it climbs |
|---|---|---|
| `frames_complete` | climbing steadily | Images produced; this is the sign of life |
| `frames_dropped` | 0 | The camera produced images faster than they could be handled |
| `vospi_crc_errors` | 0 | Corrupted data from the sensor; suspect wiring |
| `vospi_sequence_errors` | 0 | Image pieces arrived out of order |
| `vospi_resyncs` | rare | Image link re-established; occasional is fine, constant is not |
| `vospi_link_stalls` | rare | Image link went quiet and was restarted |
| `cci_errors` | 0 | Failed conversations with the sensor |
| `ffc_forced_runs` | 0 | The firmware had to force recalibration because the camera was not doing it |
| `adc_overruns` | small | Dosimeter samples were not collected in time |
| `rs485_*` | 0 | RS-422 errors |
| `clock_failures`, `fatal_code` | 0 | Serious faults |

### `bus-status`

```
baud=921600 target_id=0xC7 (199) hrt=off coarse_time=0
rx: commands=0 lrt_requests=0 other_target=0 crc_errors=0 type_errors=0
tx: lrt=112 hrt=2422
```

| Field | Meaning |
|---|---|
| `target_id` | This camera's ID on the bus. Assigned as `0xC7`. |
| `hrt` | Whether the image stream is enabled |
| `coarse_time` | Timestamp from the last packet received, echoed back in telemetry |
| `rx: commands`, `lrt_requests` | Requests received and answered |
| `rx: other_target` | Packets for other cameras, correctly ignored |
| `rx: crc_errors`, `type_errors` | Corrupt or unrecognised packets; steady growth means a wiring or settings mismatch |
| `tx: lrt`, `hrt` | Telemetry and image packets sent |

The packet counters above come from the flight firmware. Other builds report
only the link speed and this camera's address.

### `stream-status`

```
stream-status[0]=87567
stream-status[1]=38400
```

Word 0 is the current image number, word 1 its size in bytes.

### Result codes

Failures print as `camera returned error N`.

| Code | Name | Meaning |
|---|---|---|
| 0 | OK | Success |
| -1 | Bad opcode | The camera does not know this command |
| -2 | Bad length | Wrong number of arguments |
| -3 | Not ready | Not available yet; usually still starting up |
| -4 | Stale frame | The image changed during transfer; ask again |
| -5 | Internal error | The reply did not fit, or something failed internally |
| -100 | Sensor communication error | Could not reach the imaging sensor |
| -101 | Sensor timeout | The sensor did not respond in time |
| -102 | Out of range | An argument was outside what the sensor accepts |
| -103 | Protocol error | The sensor replied with something unexpected |

Codes below -100 come from the imaging sensor itself. Occasional ones during
start-up are normal; persistent ones mean the sensor is not responding, and the
camera will restart it by itself.
