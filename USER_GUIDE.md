# Thermal Camera User Guide

A practical guide to running the Lepton 3.1R radiometric thermal camera, over
USB on the bench and over RS-422 for flight. No prior knowledge of the design
is assumed.

> **This is the fast test branch.** The RS-422 link runs at **2,000,000 baud**
> here, not the 921600 used for flight, which is what lets it reach the
> sensor's full frame rate. Flight builds come from
> `flight-test/rs422-compressed`. Everything else in this guide applies to
> both.

## Run it now

Over USB, on the bench:

```powershell
python .\tools\thermalcam_app.py
```

Over RS-422, the flight link:

```powershell
python .\tools\thermalcam_app.py --source rs422 --rs422-port COM34
```

Replace `COM34` with your converter's port; it changes when you replug it. To
find it, look for the FTDI device:

```powershell
python -c "import serial.tools.list_ports as p; [print(x.device, x.hwid) for x in p.comports()]"
```

Either command opens the same window. Over RS-422 it asks the camera to start
streaming by itself, so it works whatever state the camera was left in. If
nothing appears, see [when something looks wrong](#when-something-looks-wrong).

Everything the window does is also available from the command line; see
[start the command line interface](#start-the-command-line-interface).

---

- [Run it now](#run-it-now)
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

Machine-readable command and packet hex strings for integrating into other
software are in [COMMANDS.md](COMMANDS.md). How the firmware and the protocol
actually work is in the [developer README](README.md).

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

**The camera pauses about once a minute.** It closes an internal shutter to
recalibrate, which takes roughly a second and briefly freezes the image. This
is normal and required for accuracy. Saving an image or starting a recording
deliberately triggers one first, so what you capture is freshly corrected. See
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
| `--baud` | 2000000 | Ignored over USB; used on RS-422. 921600 on flight builds |
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

### Starting from scratch

The full sequence, assuming nothing is connected yet.

**1. Plug in the RS-422 converter and find its port.**

```powershell
python -c "import serial.tools.list_ports as p; [print(x.device, x.hwid) for x in p.comports()]"
```

The converter is the FTDI one, hardware ID containing `0403:6001`. Its port
number changes when you replug it, so check each time.

**2. Confirm the camera is alive and transmitting.**

```powershell
python .\tools\rs422_diagnose.py --port COM34 --mode listen --seconds 5
```

Expect roughly 79,000 bytes a second and a few hundred packet markers. Nothing
at all means the camera is unpowered, or its transmit pair does not reach the
converter's receive pair.

**3. Put the camera into a known state.**

```powershell
python .\tools\stp_monitor.py --port COM34 --command stop-record --seconds 3
```

The image stream stops and vitals continue once a second. This matters because
the bench firmware starts streaming on power-up, so without this you cannot
tell a working command from a stream that was already running.

**4. Check the vitals decode.**

```powershell
python .\tools\stp_monitor.py --port COM34 --seconds 5
```

```
LRT  up=   885.7s  streaming  gen=7496  dose=-61.387 rad  ffc   0.0s  idle   scene 23.9..39.9 C
```

`streaming` is the imaging sensor, `idle` is the image link. Both are what you
want at this point.

**5. Ask for one image.**

```powershell
python .\tools\stp_monitor.py --port COM34 --command take-image --seconds 12 --save-frames captures\rs422
```

The camera corrects the image, sends one compressed frame, and goes back to
idle. You should see `1 frames reassembled`. A single image is always a
keyframe, so it takes around 13 packets rather than the 9 a streamed frame
averages.

**6. Or stream continuously.**

```powershell
python .\tools\stp_monitor.py --port COM34 --command start-record --save-frames captures\rs422
```

Stop it with Ctrl-C locally, and stop the camera with:

```powershell
python .\tools\stp_monitor.py --port COM34 --command stop-record --seconds 3
```

**7. Or watch it in the window instead.**

```powershell
python .\tools\thermalcam_app.py --source rs422 --rs422-port COM34
```

Turn a saved frame into a picture:

```powershell
python .\tools\render_frame.py captures\rs422\frame-00001.raw -o shot.png --destripe
```

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
| Baud | 2,000,000 on this branch, 921600 on flight builds. 8 data bits, no parity, 1 stop bit |
| Byte order | Big endian. The sync word `0x1ACFFC1D` goes out as `1A CF FC 1D` |
| Checksum | CRC-16/CCITT-FALSE: polynomial `0x1021`, seed `0xFFFF`, no reflection, no final xor |
| Checksum position | The **last two bytes of every packet**, in both directions |
| Checksum coverage | Everything after the four sync bytes, up to but not including the checksum |
| This camera's Target ID | `0xC7`, 199 decimal |

If nothing decodes, check the wiring and the baud rate first. The
`--little-endian` and `--crc-seed` options exist only so a mismatch can be
proved from the ground without rebuilding firmware; they should not be needed.
These settings live in `include/protocol/stp_protocol.h` if they ever change.

### What the link carries

Two streams share the RS-422 bus.

**Vitals** arrive once a second: uptime, what the camera is doing, the
dosimeter reading, how long since the last shutter correction, the scene's
coldest, hottest and centre temperatures, and a block of health counters.
`stp_monitor.py` prints one line per second from these.

**Images** arrive only when the camera is streaming or has been asked for a
picture. They are compressed, and are decoded for you by the window and by
`stp_monitor.py`.

The exact byte layout of both, and how the compression works, are in the
[developer README](README.md#the-rs-422-wire-format) -- you only need them if
you are writing your own decoder or integrating with the flight computer.

### Compression and frame rate

Images are compressed **losslessly**. Every pixel you get is exactly the value
the sensor produced: nothing is approximated and the radiometry is untouched.
Each frame carries a checksum of the original pixels, so the tools verify this
on every frame rather than assuming it, and tell you if one fails.

Compression is what makes the flight link usable. A raw frame is 38,400 bytes,
so uncompressed video managed 1.85 frames a second. Real scenes compress about
3.7x, and the measured rate is now:

| Link | Frames per second |
|---|---|
| USB video | 8.8, the sensor's full rate |
| RS-422 at 2 Mbaud, this branch | 8.5, essentially the full rate |
| RS-422 at 921600, the flight rate | about 5.8 |
| RS-422, uncompressed (before this) | 1.85 |

**On this branch RS-422 keeps up with the sensor.** At the flight rate of
921600 it does not: the link runs out of bandwidth first, and roughly one frame
in three is skipped.

Expect the rate to move around a little. The camera compresses each frame as it
sends it, and how well a scene compresses depends on what is in front of the
lens, so a busy or fast-changing scene runs slower than a still one.

You may see the rate dip briefly. A single image is always sent in full rather
than as a difference from the previous one, so `take-image` costs more than a
streamed frame, and the camera sends a full frame every twelve frames anyway so
that a lost packet cannot spoil the picture for more than about two seconds.

### Watch RS-422 images in the window

The graphical interface can take its pictures from RS-422 instead of USB, which
is how you see the flight link the way the flight computer does:

```powershell
python .\tools\thermalcam_app.py --source rs422 --rs422-port COM34
```

Everything in the window works as usual: palettes, contrast, click-to-measure,
saving images and recording. Three differences:

- The picture updates at about six frames a second rather than 8.8.
- The window asks the camera to start streaming when it connects, so it does
  not matter what state the camera was left in.
- The dosimeter panel is fed from the vitals stream rather than polled. The
  correction and dosimeter-zero buttons work over RS-422; images and recordings
  are saved from the stream on the computer, not on the camera.

| Option | Meaning |
|---|---|
| `--source rs422` | Take pictures from RS-422 |
| `--rs422-port` | The converter's port, required with `--source rs422` |
| `--rs422-baud` | Default 2000000 on this branch, 921600 on flight builds |
| `--target` | Camera Target ID. Default `0xC7` |

### Asking the camera for images

Streaming runs at about six frames a second, which is fine for watching but
still short of the sensor's 8.8, and worse if the link is busy or slow. The
commands are therefore discrete, so you
ask for exactly what you want rather than turning a general stream on and off:

```powershell
python .\tools\stp_monitor.py --port COM34 --command take-image
python .\tools\stp_monitor.py --port COM34 --command start-record
python .\tools\stp_monitor.py --port COM34 --command stop-record
```

| Command | Corrects first | What it does |
|---|---|---|
| `run-ffc` | - | Correct the image now |
| `take-image` | yes | Send exactly one complete frame, then stop. Use when streaming is too slow to be useful |
| `start-record` | yes | Stream continuously until stopped |
| `stop-record` | - | Stop streaming |
| `stream-on` | - | Stream without correcting, when the image is already settled |
| `stream-off` | - | Stop streaming |
| `dosimeter-zero` | - | Measure and store this unit's dosimeter zero |

The ones that capture correct the image first and wait for the shutter, so what
comes back is not a drifted frame. Vitals report the shutter age and whether
the camera is `idle`, `correcting`, sending a `single image`, or `recording`,
so you can see a request being taken up.

The complete packet bytes for each are in [COMMANDS.md](COMMANDS.md).

### If the camera will not receive

Fixed 2026-08-21. The symptom was that the camera transmitted perfectly --
thousands of packets with no checksum failures -- while nothing sent
to it ever arrived. Its received-packet counters stayed at zero, including the
corrupt-packet counters, which is the signature of a wiring fault rather than a
baud rate or firmware problem: a wrong baud rate still counts corrupt bytes.

The cause was cross-wiring. The camera's receive pair (A and B) had been landed
on the converter's *receive* terminals, so both ends were listening and neither
was talking to the other. Correct is receive-to-transmit in both directions:

| Camera (ADM2582E) | Converter |
|---|---|
| A, B (receiver inputs) | TX+, TX- |
| Y, Z (driver outputs) | RX+, RX- |

If commands are ignored again, check in this order:

1. **The pairs are not swapped**, per the table above. This is the fault that
   actually occurred.
2. **The 120 ohm termination across A and B is populated.** Removing it makes
   things worse, not better; a floating pair sits at several volts.
3. **The receiver-enable pin is active.** On this board RE is tied to ground,
   so it is always enabled.
4. **The grounds are joined.** The link is not isolated here.
5. **Nothing else holds the port open.** A stale monitor or window process
   keeps the converter's port and silently swallows the traffic.

Confirm with the diagnostic, which reports bytes and packet markers per second:

```powershell
python .\tools\rs422_diagnose.py --port COM34 --mode listen --seconds 5
```

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

The camera recalibrates **once a minute**, and sooner if its temperature
drifts. You do not normally need to do anything: saving an image and starting a
recording both trigger a correction first and wait for it to finish, so a
capture is never of a drifted image.

The camera's own shutter timer runs at three minutes and cannot be shortened
safely, so the firmware asks for a correction itself once a minute. That is why
`ffc-status` shows a three-minute period while corrections actually happen
every minute.

Force one at any other time if the image looks smeared:

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
| Camera ignores RS-422 commands | Check the wiring; see [if the camera will not receive](#if-the-camera-will-not-receive). |

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
baud=2000000 target_id=0xC7 (199) hrt=off coarse_time=0
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
