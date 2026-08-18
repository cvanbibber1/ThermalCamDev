---
type: hardware-map
status: review
last-reviewed: 2026-08-17
---

# STM32F412CGU6 pin map

The `U` package code is UFQFPN48. Alternate functions below are valid for the
STM32F412Cx family.

| MCU pin | Board signal | Intended mode | Peripheral / AF | Review status |
|---|---|---|---|---|
| PA0 | LEPTON_GPIO2 | Floating/high-impedance | GPIO/analog input | Accepted as an unused floating MCU pin |
| PA1 | LEPTON_GPIO0 | Floating/high-impedance | GPIO/analog input | Accepted as an unused floating MCU pin |
| PA2 | RS485_TXD | Output | USART2_TX, AF7 | Accepted, assuming this is ADM2582E TxD |
| PA3 | RS485_RXD | Input | USART2_RX, AF7 | Accepted, assuming this is ADM2582E RxD |
| PA4 | DOSIMETER_ADC | Analog input | ADC1_IN4 | Accepted; `DOSI` is renamed |
| PA7 | LEPTON_GPIO1 | Floating/high-impedance | GPIO/analog input | Accepted as an unused floating MCU pin |
| PA8 | LEPTON_PWR_DWN_L | Output | GPIO | Accepted; Lepton clock is a separate always-on source |
| PA9 | LEPTON_RESET_L | Output | GPIO | Accepted; USB connector supplies data only and VBUS sensing is disabled |
| PA11 | USB_DM | USB AF | OTG_FS_DM, AF10 | Accepted |
| PA12 | USB_DP | USB AF | OTG_FS_DP, AF10 | Accepted |
| PA13 | SWDIO | Debug | SYS/JTMS-SWDIO | Reserved for SWD |
| PA14 | SWCLK | Debug | SYS/JTCK-SWCLK | Reserved for SWD |
| PB7 | RS485_DE | Output | GPIO | Accepted after hardware pull-up is removed |
| PB8 | LEPTON_SCL | Open-drain | I2C1_SCL, AF4 | Accepted; pull up to Lepton VDDIO |
| PB9 | LEPTON_SDA | Open-drain | I2C1_SDA, AF4 | Accepted; pull up to Lepton VDDIO |
| PB12 | LEPTON_CS_L | Output | GPIO preferred | Accepted; manual packet/segment control |
| PB13 | LEPTON_SPI_CLK | Output | SPI2_SCK, AF5 | Accepted; 12.5 MHz baseline |
| PB14 | LEPTON_SPI_MISO | Input | SPI2_MISO, AF5 | Accepted |
| PB15 | LEPTON_SPI_MOSI | Low output | GPIO low preferred | Lepton does not use MOSI; keep low |
| PC13 | LEPTON_VSYNC | Input | GPIO EXTI13 | Accepted; verify observed pulse cadence |
| PH0 | HSE_IN | Clock input | OSC_IN bypass | MAX7375AXR805 supplies the MCU's nominal 8 MHz HSE |
| BOOT0 | BOOT0 | 10 kOhm pulldown | System boot strap | Accepted |
| NRST | TARGET_NRST | Reset input | Reset | FT2232 ADBUS4 plus normal reset network |

## Accepted clock architecture

The accepted MCU clock architecture uses:

- Accurate 8 MHz external oscillator on `PH0`, HSE bypass mode.
- Main PLL: `PLLM=8`, `PLLN=200`, `PLLP=2` for 100 MHz SYSCLK.
- PLLI2S: `PLLI2SM=8`, `PLLI2SN=192`, `PLLI2SQ=4` for USB 48 MHz.
- `CK48MSEL = PLLI2SQ`.
- A separate always-on clock source supplies the Lepton `MASTER_CLK`; it is not derived
  from the MAX7375 or PA8.

The MAX7375AXR805 is exclusively the STM32 HSE source. Its suitability for the MCU and
USB clock tree will be validated on hardware by measuring HSE-derived clocks and USB
enumeration over voltage and temperature. The independent Lepton clock must be checked
at the Lepton connector for frequency, duty cycle, and startup timing.

## Electrical notes

- Lepton VDDIO must be 2.8 to 3.1 V. The 2.2 kOhm CCI pull-ups must terminate at
  that rail, not at 3.3 V.
- Lepton GPIO0, GPIO1, and GPIO2 are unused. Their MCU-side pins remain in analog mode
  with no pull so the board connections are electrically high impedance.
- Verify all MCU-to-Lepton outputs against the Lepton VDDIO domain. Level translation
  is preferred if the MCU remains at 3.3 V.
- Lepton MOSI is unused and should be grounded or held low.
- Add defined hardware-safe states for `RESET_L`, `PWR_DWN_L`, `CS_L`, and `RS485_DE`.
- `RS485_DE` must have a pulldown so the ADM2582E driver is disabled during reset.
- USB VBUS is not routed. Firmware disables VBUS sensing and treats USB as a data-only,
  self-powered device connection; system-level USB compliance remains a hardware review item.
- Retain PA13 and PA14 exclusively for SWD during development.

## Debug mapping

Per [[../../FT2232 Information]]:

- FT2232H Channel A ADBUS0/TCK -> PA14/SWCLK.
- ADBUS1/TDI -> PA13/SWDIO through 470 Ohm.
- ADBUS2/TDO -> PA13/SWDIO directly.
- ADBUS4 -> NRST.
- Common ground and target voltage reference are required.

FT2232H Channel B has no conflict-free UART assignment yet because USART2 on PA2/PA3
is allocated to the field bus. Prefer USB CDC or PB3/SWO for early logging unless a second
UART is explicitly routed.
