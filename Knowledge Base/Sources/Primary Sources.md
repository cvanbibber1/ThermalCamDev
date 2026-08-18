---
type: sources
status: active
last-reviewed: 2026-08-17
---

# Primary sources

Only primary manufacturer documentation should define hardware or protocol requirements.

- [STM32F412xE/xG datasheet DS11139 Rev 9](https://www.st.com/resource/en/datasheet/stm32f412cg.pdf)
- [STM32F412 reference manual RM0402](https://www.st.com/resource/en/reference_manual/rm0402-stm32f412-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [STM32F4 HAL driver](https://github.com/STMicroelectronics/stm32f4xx-hal-driver)
- [Teledyne FLIR Lepton technical documentation](https://oem.flir.com/developer/lepton-family/lepton-technical-documentation/)
- [Lepton product datasheet Rev 400](https://flir.netx.net/file/asset/13333/original/attachment)
- [Lepton Software IDD Rev 303](https://flir.netx.net/file/asset/12411/original/attachment)
- [MAX7375 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/max7375.pdf)
- [ADM2582E/ADM2587E datasheet Rev H](https://www.analog.com/media/en/technical-documentation/data-sheets/adm2582e-2587e.pdf)
- [[../../FT2232 Information|Local FT2232 hardware note]]

## Verified facts used in v0.2

- MAX7375AXR805 is nominally 8.00 MHz and specifies 2% initial accuracy; in this design
  it clocks only the STM32, not the Lepton.
- Lepton 3.1R requires a nominal 25 MHz master clock with 24.975 to 25.025 MHz range.
- Lepton VDDIO is 2.8 to 3.1 V.
- Lepton CCI address is 0x2A using 7-bit addressing.
- Lepton GPIO0/1/2 are reserved; existing MCU-side signals remain analog/no-pull and are
  otherwise ignored. GPIO3 supports VSYNC.
- Raw14 VoSPI uses mode 3, maximum 20 MHz SCK, and 164-byte packets.
- Lepton 3 valid frames contain four 60-packet segments when telemetry is disabled.
- ADM2582E supports RS-485/RS-422 operation up to 16 Mbit/s and 256 nodes by unit load.
- STM32F412 supports a PLLI2SQ-derived 48 MHz clock and MCO1 PLL output on PA8.
