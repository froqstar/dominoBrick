# dominoBrick wiring — ESP32-WROOM32 to Xiegu G106

Target: classic ESP32-WROOM32 devkit + G106 rear ACC port, radio in U-D
(data) mode. Minimal BOM, single-pole filters, fixed dividers.

## ESP32 pin map

| ESP32 | G106 ACC | Notes |
|---|---|---|
| GPIO25 (DAC_CHAN_0) | AF IN (via TX network) | `PIN_DAC_TX`, 8 kHz DMA |
| GPIO36/VP (ADC1 CH0) | AF OUT (via RX network) | `PIN_ADC_RX`, input-only |
| GPIO4 | PTT (via NPN switch) | `PIN_PTT_GPIO`, active-low (0 = keyed) |
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
             1u          R1 3.9k         | F        R2 10k          | D
                                        |                         |
                                       C1                        R3
                                      22nF                      2.7k
                                        |                         |
GND o-----------------------------------o-------------------------o----o G106 GND
```

F = filter node (C1 hangs from F to GND), D = divider node (R3 hangs
from D to GND, D also feeds the radio).

## RX: G106 AF OUT -> GPIO36

Cap blocks radio DC, R4/C2 is the anti-alias low-pass (~1.9 kHz
loaded), R5/R6 centers the audio at 1.65V for the 0–3.3V ADC
(-1.6 dB loss, negligible). R4 also limits fault current into the pin.

```text
G106 AF OUT o----||----------/\/\/\----------o GPIO36
                 1u         R4 10k          | F
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

## PTT: GPIO4 -> G106 PTT

GPIO4 idles high, pulls low to key. Don't wire it straight to the
radio — switch it through an NPN so radio-side pull-ups can't
back-feed the ESP32.

```text
GPIO4 o----/\/\/\----+----o NPN base (2N2222 / BC547)
            R7 1k     |
                      +----/\/\/\---- GND
                      R8 10k (pull-down, keeps PTT off at boot)

NPN emitter o---- GND
NPN collector o---- G106 PTT
```

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
| R1 | 3.9k | TX low-pass series | With C1: fc ~1.9 kHz |
| C1 | 22nF ceramic | TX low-pass shunt | X7R fine |
| R2 | 10k | TX divider top | With R3: ~550mVp-p out |
| R3 | 2.7k | TX divider bottom | Swap for 10k trim pot for adjustable level |
| C_RX | 1uF ceramic | RX DC block | X7R fine |
| R4 | 10k | RX low-pass series + current limit | With C2: fc ~1.6 kHz |
| C2 | 10nF ceramic | RX low-pass shunt | X7R fine |
| R5, R6 | 100k x2 | RX bias to 1.65V | 1% or 5% both fine |
| Q1 | 2N2222 / BC547 NPN | PTT switch | Any small-signal NPN |
| R7 | 1k | PTT base resistor | — |
| R8 | 10k | PTT pull-down | Holds PTT off during boot |
| J_CAT | 3.5mm stereo plug + cable | CAT serial to G106 COM | Buzz out TIP/RING first |
| R_CAT1, R_CAT2 | 1k x2 | CAT series protection | GPIO17/16 to COM |

13 parts for audio + PTT (+3 more for CAT). No op-amp, no diodes at
this BOM level. Every value is E12 jellybean, available as 0805 SMD
(JLC "basic parts" level) or through-hole — no precision or
high-voltage parts anywhere.

## Optional upgrades (only if needed)

| Add | When |
|---|---|
| 10k trim pot instead of R2/R3 | Level trim without soldering |
| BAT54S clamp pair on GPIO36 (to 3V3/GND) | Loud-signal resets or clipping |
| MCP6002 follower between TX filter and divider | Driving long cables or low-Z inputs |
| Second RC pole on TX/RX | If spurs/aliases show on scope |

## Bring-up notes

- COM serial: loopback-test the cable at 19200 first, then probe
  `19 00` and expect rig ID `01 06`. Knob turns should surface as
  FREQ notifies within ~2 s (1 s poll + snoop).

- Common ground between ESP32 and G106 is required.
- G106: U-D mode, set AUX IN volume mid-scale first, then check TX level.
- Scope TX at G106 AF IN: expect ~350mVp-p of clean ~1 kHz tones, no flat-topping.
- Scope RX at GPIO36: idle ~1.65V DC, speech/data swinging ~1Vpp around it, never hitting 0V or 3.3V rails.
