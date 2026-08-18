## FT2232H SWD Debug / Programming Interface Changes

Implement the **CJMCU-2232HL (FT2232H)** as the primary USB programming and debugging interface for the STM32F4. Use **FT2232H Channel A for SWD/MPSSE** and reserve **Channel B for a UART serial console**.

### Required FT2232H-to-STM32F4 Connections

Use the following Channel A pin assignment:

- `ADBUS0 / TCK` → STM32 `PA14 / SWCLK`
- `ADBUS1 / TDI` → STM32 `PA13 / SWDIO` through a **470 Ω series resistor**
- `ADBUS2 / TDO` → STM32 `PA13 / SWDIO` directly
- `ADBUS4` → STM32 `NRST`
- `GND` → STM32 `GND`

SWDIO must be formed by combining the FT2232H's separate MPSSE output and input signals:

```text
ADBUS1 / TDI ---[470R]---+
                         +---- STM32 PA13 / SWDIO
ADBUS2 / TDO ------------+
```

Do not connect only one FT2232H pin to SWDIO. The FT2232H MPSSE implementation requires separate output and input paths.

### Optional UART Debug Interface

Reserve FT2232H Channel B as a standard UART interface:

- `BDBUS0 / TXD` → STM32 UART RX
- `BDBUS1 / RXD` ← STM32 UART TX
- Common GND

This should allow simultaneous:

- SWD programming
- GDB debugging
- Reset control
- UART logging / command console

from a single USB connection.

### STM32F4 Debug Signals

Expose at minimum:

- `SWDIO / PA13`
- `SWCLK / PA14`
- `NRST`
- `GND`
- `3.3 V / VTREF`

Preferably also expose:

- `SWO / PB3`

Use a standard **10-pin 1.27 mm ARM Cortex SWD connector** where PCB space permits so the target remains compatible with ST-Link, J-Link, and other standard SWD debuggers.

Keep any FT2232H-specific SWDIO combining circuitry on the FT2232 adapter side rather than the STM32 target side.

### Reset / Boot Configuration

Keep `NRST` connected to the debugger so **connect-under-reset** is possible if firmware disables SWD, enters a low-power mode, crashes during startup, or otherwise prevents normal debugger attachment.

Use the normal recommended STM32 reset network, approximately:

```text
3.3V
 |
10k
 |
 +----- NRST ----- debugger
 |
100nF
 |
GND
```

Verify exact values against the selected STM32F4 datasheet/reference design.

Configure `BOOT0` for normal flash boot, normally with a pulldown such as:

```text
BOOT0
 |
10k
 |
GND
```

BOOT0 does not need to be controlled by the programmer for normal SWD programming.

### Signal Integrity

Do not place significant capacitance on SWCLK or SWDIO.

Optionally provide footprints for approximately **22–47 Ω series damping resistors** on SWCLK/SWDIO if needed for longer cables or higher SWD speeds.

The **470 Ω resistor between ADBUS1/TDI and SWDIO is required for the FT2232H SWD implementation** and is separate from optional signal-integrity resistors.

### Voltage / Power Rules

Assume STM32F4 debug I/O operates at **3.3 V**.

Verify the CJMCU-2232HL FT2232H VCCIO configuration before connection.

Do not directly connect two independently regulated 3.3 V outputs together.

Prefer powering the STM32 target independently unless the CJMCU board's power architecture has been explicitly verified.

The debugger should share:

- GND
- SWDIO
- SWCLK
- NRST

with the target.

Expose target `3.3 V / VTREF` on the SWD connector for compatibility with conventional debug probes.

### OpenOCD Configuration

Design the interface to work with OpenOCD's FTDI/MPSSE driver using Channel A.

Use approximately this configuration as the starting point:

```tcl
adapter driver ftdi
adapter usb vid_pid 0x0403 0x6010

ftdi channel 0

ftdi layout_init 0x0000 0x000b

ftdi layout_signal nSRST -data 0x0010 -oe 0x0010

ftdi layout_signal SWD_EN -data 0
ftdi layout_signal SWDIO_OE -data 0

transport select swd

adapter speed 1000

reset_config srst_only srst_nogate
```

Begin at approximately **1 MHz SWD clock** for bring-up. After confirming reliable operation, test:

```text
2 MHz
4 MHz
8 MHz
```

Use approximately **4 MHz** as the preferred conservative working speed unless testing shows higher speeds are consistently reliable.

### Programming Workflow

Support standard OpenOCD STM32F4 programming using ELF binaries:

```bash
openocd \
  -f cjmcu-ft2232h-swd.cfg \
  -f target/stm32f4x.cfg \
  -c "program firmware.elf verify reset exit"
```

For raw binary files:

```bash
openocd \
  -f cjmcu-ft2232h-swd.cfg \
  -f target/stm32f4x.cfg \
  -c "program firmware.bin 0x08000000 verify reset exit"
```

Prefer ELF files where possible because load addresses are embedded in the file.

The intended normal programming sequence is:

```text
Connect
→ Halt STM32
→ Erase required flash
→ Program
→ Verify
→ Reset
→ Run
```

### Development Requirements

Ensure firmware development does not unnecessarily reconfigure:

- `PA13 / SWDIO`
- `PA14 / SWCLK`

during development.

Retain NRST access so the MCU can still be recovered with connect-under-reset if these pins are accidentally reconfigured.

Do not enable irreversible debug-security settings such as permanent high-level readout protection during development.

### Final Intended Architecture

```text
                         USB
                          |
                     FT2232H
                 +--------+--------+
                 |                 |
             Channel A         Channel B
               MPSSE              UART
                 |                 |
        +--------+------+       TX / RX
        |        |      |          |
      SWCLK    SWDIO   NRST        |
        |        |      |          |
        +--------+------+----------+
                 |
              STM32F4
```

The finished design should provide **reliable STM32F4 flash programming, interactive SWD/GDB debugging, reset/recovery capability, and simultaneous UART diagnostics over one FT2232H USB interface**.