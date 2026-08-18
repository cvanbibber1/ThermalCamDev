# ThermalCamDev

Firmware and interface specification for a radiometric thermal camera built around an
STM32F412CGU6 and a Teledyne FLIR Lepton 3.1R.

The repository contains a buildable STM32Cube/HAL firmware baseline with Lepton CCI and
VoSPI capture, USB UVC Y16 + CDC control, dosimeter ADC telemetry, and a provisional
four-wire multidrop transport. The design uses a bare-metal event loop and static runtime
buffers.

Start here:

- [[Knowledge Base/Home|Project memory and navigation]]
- [[Knowledge Base/Architecture/System Specification v0.1|System specification v0.2]]
- [[Knowledge Base/Project/Risks and Bugs|Hardware blockers and risks]]
- [[Knowledge Base/Project/Implementation Status|Implementation and test status]]
- [[FT2232 Information|FT2232 SWD/debug notes]]

`Knowledge Base/` is an Obsidian-compatible vault. `graphify-out/` is the generated
Graphify knowledge graph and must be regenerated after material project changes.

## Build and test

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-dev.txt
.\.venv\Scripts\pio.exe run -e target
.\.venv\Scripts\pio.exe test -e native --without-uploading
```

The native test requires a C compiler on `PATH`. On Windows, the tested compiler can be
installed with:

```powershell
winget install --id BrechtSanders.WinLibs.POSIX.UCRT --exact
```

The target artifacts are `.pio/build/target/firmware.elf` and `firmware.bin`.

## Host control

The same command protocol is available on USB CDC and USART2/ADM2582E:

```powershell
.\.venv\Scripts\python.exe .\tools\thermalcam_cli.py --port COM12 info
.\.venv\Scripts\python.exe .\tools\thermalcam_cli.py --port COM12 dosimeter
.\.venv\Scripts\python.exe .\tools\thermalcam_cli.py --port COM12 frame --output frame-y16.raw
```

The development USB VID/PID is `1209:F412`; a production allocation is still required.
