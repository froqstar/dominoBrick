# dominoBrick wiring — ESP32-WROOM32 to Xiegu G106

Target: classic ESP32-WROOM32 devkit + G106 rear ACC port, radio in U-D
(data) mode. Minimal BOM, single-pole filters, fixed dividers.

## ESP32 pin map

| ESP32 | G106 ACC/KEY | Notes |
|---|---|---|
| GPIO25 (DAC_CHAN_0) | AF IN (via TX network) | `PIN_DAC_TX`, 8 kHz DMA |
| GPIO34 (ADC1 CH6) | AF OUT (via RX network) | `PIN_ADC_RX`, input-only |
| GPIO4 | PTT (via PC817 optocoupler) | `PIN_PTT_GPIO`, active-low (0 = keyed) |
| GPIO5 | KEY TIP (via PC817 optocoupler) | CW key out, active-low (optional) |
| GPIO16 | G106 COM TIP (radio RXD) | `CAT_PIN_TX`, UART 19200 8N1 via 1k series |
| GPIO17 | G106 COM RING (radio TXD) | `CAT_PIN_RX`, UART 19200 8N1 via 1k series |
| GND | GND | Common ground, mandatory |

## TX: GPIO25 -> G106 AF IN

DAC puts out ~2.6Vpp centered on ~1.65V DC. The cap blocks the DC,
R1/C1 is the reconstruction low-pass (~2.4 kHz loaded), R2/R3 divides
down to ~340mVp-p into a 10k load (stays well under the ~600mVp-p
LINE IN limit; higher-Z inputs see up to ~430mVp-p).

```text
GPIO25 o----||----------/\/\/\----------o----------/\/\/\----------o G106 AF IN
             1u          R1 3.9k        | F        R2 10k          | D
                                        |                          |
                                       C1                          R3
                                      22nF                        2.7k
                                        |                          |
GND o-----------------------------------o--------------------------o---o G106 GND
```

F = filter node (C1 hangs from F to GND), D = divider node (R3 hangs
from D to GND, D also feeds the radio).

## RX: G106 AF OUT -> GPIO34

Cap blocks radio DC, R4/C2 is the anti-alias low-pass (~1.9 kHz
loaded), R5/R6 centers the audio at 1.65V for the 0–3.3V ADC
(-1.6 dB loss, negligible). R4 also limits fault current into the pin.

```text
G106 AF OUT o----||----------/\/\/\----------o GPIO34
                 1u         R4 10k           | F
                                             |
                        +--------------------+--------------------+
                        |                    |                    |
                    C2 to GND            R5 to 3V3            R6 to GND
                      10nF                 100k                 100k
                        |                    |                    |
                       GND                  3V3                  GND
```

All three branches hang off node F: C2 down to GND, R5 up to 3V3,
R6 down to GND.

## PTT: GPIO4 -> G106 PTT (PC817 optocoupler)

GPIO4 idles high (LED off, collector open, radio sees key-up via its
internal pull-up). GPIO4 low -> LED on -> collector conducts -> PTT
pulled to GND -> key-down. Galvanic isolation: no electrical path
between ESP32 and radio on the PTT line.

```text
ESP side (LED, isolated from radio):
GPIO4 o----/\/\/\----o pin 1 (LED anode)
            R7 220Ω   |
                    pin 2 (LED cathode) o---- GND

Radio side (phototransistor, isolated from ESP):
G106 PTT o----/\/\/\----o pin 4 (collector)
            R8 4.7k (opt)   |
                          pin 3 (emitter) o---- GND
```

R8 (4.7k) is optional — the radio's internal pull-up handles key-up
when the optocoupler is off. Add it only if the radio floats the PTT
line instead of pulling it up (measure first, then decide).

## KEY: GPIO5 -> G106 KEY TIP (PC817 optocoupler, optional)

Same circuit as PTT, driving the KEY jack tip instead. Only needed
if you want the ESP32 to key CW through the radio's KEY input
rather than through PTT. KEY jack is a 3.5mm stereo: TIP = dot/
straight key, RING = dash, SLEEVE = GND.

```text
ESP side (LED, isolated from radio):
GPIO5 o----/\/\/\----o pin 1 (LED anode)
            R9 220Ω    |
                    pin 2 (LED cathode) o---- GND

Radio side (phototransistor, isolated from ESP):
KEY TIP o----/\/\/\----o pin 4 (collector)
            R10 4.7k (opt)    |
                          pin 3 (emitter) o---- GND
```

For iambic paddle (keyer mode), add a second identical circuit on
another GPIO driving KEY RING. Firmware changes needed in the
iambic logic — not just a second optocoupler.

## CAT: GPIO16/17 -> G106 COM (3.5mm)

COM jack is a 3.5mm stereo TX/RX/GND serial port, 19200 8N1 CI-V
(rig addr `0x70`). Separate cable from the ACC audio/PTT cable.
UART1 stays free (UART0 = console); UART2 remapped to GPIO16/17.
Pinout: TIP = radio RXD (driven by GPIO17/ESP TX),
RING = radio TXD (feeds GPIO16/ESP RX), SLEEVE = GND.
Xiegu labels serial from adapter side, so TIP "TxD" = adapter TX = radio RX.
Blue USB cable reference: TX green -> TIP = RX-data, RX white -> RING = TX-data.
Levels (measured): RING idles at 3.3V, so the 1k series resistor
straight into GPIO16 is safe — no divider needed.

```text
GPIO16 (ESP TX) o----/\/\/\----o TIP (radio RXD, 1k series)
GPIO17 (ESP RX) o----/\/\/\----o RING (radio TXD, 1k series)
ESP GND         o-----------------o SLEEVE (GND, shared with ACC GND)
```

Direct connect, no level shifter at this BOM level.

## BOM (minimal build)

| Ref | Value | Purpose | Notes |
|---|---|---|---|
| C_TX | 1uF ceramic (50V, X7R) | TX DC block | No polarity, high-pass ~10 Hz |
| R1 | 3.9k | TX low-pass series | With C1: fc ~2.4 kHz loaded |
| C1 | 22nF ceramic | TX low-pass shunt | X7R fine |
| R2 | 10k | TX divider top | With R3: ~340mVp-p into 10k |
| R3 | 2.7k | TX divider bottom | Swap for 10k trim pot for adjustable level |
| C_RX | 1uF ceramic | RX DC block | X7R fine |
| R4 | 10k | RX low-pass series + current limit | With C2: fc ~1.9 kHz loaded |
| C2 | 10nF ceramic | RX low-pass shunt | X7R fine |
| R5, R6 | 100k x2 | RX bias to 1.65V | 1% or 5% both fine |
| U1 | PC817 optocoupler | PTT switch | Galvanic isolation, any CTR |
| R7 | 220Ω | PTT LED drive | ~10mA from 3.3V GPIO |
| U2 | PC817 optocoupler | KEY switch (optional) | Same circuit as PTT |
| R9 | 220Ω | KEY LED drive (optional) | Same value as R7 |
| J_CAT | 3.5mm stereo plug + cable | CAT serial to G106 COM | Buzz out TIP/RING first |
| R_CAT1, R_CAT2 | 1k x2 | CAT series protection | GPIO17/16 to COM |

16 parts for audio + PTT + KEY (+3 more for CAT). No op-amp, no
diodes, no NPN transistors — only optocouplers for PTT/KEY. Every
value is E12 jellybean, available as 0805 SMD (JLC "basic parts"
level) or through-hole. R8 and R10 (collector pull-ups) are
omitted — the radio's internal pull-ups handle key-up; measure
first, then add only if the PTT/KEY line floats instead of pulling
up.

## Optional upgrades (only if needed)

| Add | When |
|---|---|
| 10k trim pot instead of R2/R3 | Level trim without soldering |
| BAT54S clamp pair on GPIO34 (to 3V3/GND) | Loud-signal resets or clipping |
| MCP6002 follower between TX filter and divider | Driving long cables or low-Z inputs |
| Second RC pole on TX/RX | If spurs/aliases show on scope |

## Flashing header (bare SMD module, no USB)

6-pin header, doubles as UART0 console after assembly. External
3.3V supply feeds the board through this header during flashing;
external USB-UART must be 3.3V logic (CP2102/CH340/FTDI) — never
5V. Manual BOOT + EN buttons, no auto-reset circuit.

```text
Header (board side):

  1  2  3  4  5  6
 GND TX0 RX0 IO0 EN 3V3

 1 GND ......... supply return, common with USB-UART GND
 2 TX0 ......... GPIO1, to USB-UART RX
 3 RX0 ......... GPIO3, to USB-UART TX (crossed)
 4 IO0 ......... GPIO0, BOOT button to GND + 10k to 3V3
 5 EN .......... CHIP_PU, EN button to GND + 10k to 3V3 + 100nF to GND
 6 3V3 ......... external 3.3V in (>=500mA, TX peaks), 100nF + 10uF at module
```

Buttons (one each on IO0 and EN):

```text
3V3 o----/\/\/\----o IO0 (or EN) ----o____o---- GND
             10k    |                button
                    |
                   (+) 100nF on EN only, to GND
```

Flash procedure: hold BOOT -> tap EN -> release BOOT ->
`idf.py -p PORT flash`. Normal boot needs IO0 high, so the 10k
pull-up is load-bearing — a stuck BOOT button parks the module
in download mode forever.

Strapping constraints (do not violate on the carrier board):

- GPIO2 must stay floating (no pull-up). GPIO0 low + GPIO2 high
  selects an unsupported boot mode and flashing fails.
- No pull-down on GPIO5 (KEY optocoupler LED + 220 Ohm to GND
  is fine: pin is input at reset, internal pull-up reads high).
- GPIO0 doubles as firmware BOOT test button (`PIN_BOOT_BUTTON`,
  active-low with internal pull-up in `app_main.c`) — the header
  button and the firmware button are the same physical button.

## Bring-up notes

- COM serial: loopback-test the cable at 19200 first, then probe
  `19 00` and expect rig ID `01 06`. Knob turns should surface as
  FREQ notifies within ~2 s (1 s poll + snoop).

- Common ground between ESP32 and G106 is required.
- G106: U-D mode, set AUX IN volume mid-scale first, then check TX level.
- Scope TX at G106 AF IN: expect ~350mVp-p of clean ~1 kHz tones, no flat-topping.
- Scope RX at GPIO34: idle ~1.65V DC, speech/data swinging ~1Vpp around it, never hitting 0V or 3.3V rails.
