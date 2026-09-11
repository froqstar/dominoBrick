# dominoBrick PLAN

Goal: ESP32 talks DominoEX11 with Xiegu G106 ACC port. RX decode + TX encode audio. Text transport, CAT tune, CW later. Out of scope this phase.

## Decisions locked
- MCU: classic ESP32-WROOM32 devkit. Reason: 2x8-bit builtin DAC (GPIO25/26) plus BT/BLE. S2 has no Bluetooth, S3/C3/C6 lack DAC. No external codec for v1.
- Framework: ESP-IDF. Reason: I2S/DAC/DMA + timing control.
- PTT: ACC PTT pin via opto. No VOX. Reliable keying.
- Mode: DominoEX11 no FEC first. Default mode, simpler.
- Audio: internal only. DAC TX + SAR ADC RX. Noisy but ok for prototype. Later jump to PCM5102A + PCM1808 if needed.

## DominoEX11 facts
- 10.766 baud, 92.88ms symbol, 18 tones, spacing ~10.77Hz, BW ~262Hz.
- USB mode, audio center ~1kHz. Verify vs fldigi.
- IFK: data = tone difference, not absolute tone. Tolerates 200Hz+ offset/drift.
- IFK+: offset table forbids same/adjacent reuse. Cuts ISI from multipath.
- Code: nibbles -> secondary varicode -> ASCII. No FEC v1.
- Source truth: `fldigi/src/dominoex/` tables. Port verbatim. Do not reinvent.

## Hardware needed
- 8-pin miniDIN plug + shielded cable to G106 ACC. Verify AF_IN, AF_OUT, PTT, GND with manual + multimeter before power. Wrong wire kills port.
- 2x 600:600 audio isolation transformers (RX + TX). Breaks ground loop / hum. DE-19 likely lacks these. Do not skip.
- 1x PC817 opto for PTT. S3 GPIO -> 1k -> opto LED. Opto transistor -> ACC PTT to GND. Active low.
- Pots = trimmer potentiometers, 10k log/linear. One TX level, one RX level. Sets drive without overdrive/clip/ALC.
- RC lowpass both directions, ~3kHz cutoff. DAC outputs stair-steps. RC smooths to clean sine, kills harmonics/splatter. ADC side RC stops alias fold into Domino tones.
- R/C kit: 1uF DC-block caps, divider resistors, ferrite on ACC cable, star GND.
- Supplies: G106 9-15V 3A separate from USB. Common RF ground via transformer only.

### Wiring
```
G106 AF_OUT -> pot divider -> 1uF -> transformer -> RC 3kHz -> ESP32 ADC GPIO36
ESP32 DAC GPIO25 -> RC 3kHz -> pot -> transformer -> 1uF -> G106 AF_IN
ESP32 GPIO4 -> 1k -> PC817 -> G106 PTT/GND
```
- Start TX ~50mVpp. Watch ALC. Raise slow.
- Shield only one end. Keep digital + audio GND split except via transformer.

## Software (ESP-IDF)
1. `audio_io`: ADC continuous 8kHz mono DMA ring. DAC DMA 8kHz same clock. Fixed clock drives symbol timing.
2. `rx_dsp`: 18x Goertzel per 92.88ms window (~743 samples @8kHz). Peak -> tone idx. Delta `(curr-prev) mod 18` -> reverse IFK+ map -> nibble. Early-late gate for symbol sync. Bandpass around tones first.
3. `rx_decode`: nibble assembler -> varicode table -> ASCII queue. Idle tone = squelch. Ring buffer out for future USB/BLE/WiFi layer.
4. `tx_encode`: ASCII queue -> varicode -> IFK+ forward map -> NCO phase-continuous sine table. 10ms raised-cosine ramp per symbol. 500ms idle preamble.
5. `ptt_ctrl`: assert PTT, wait 150ms, stream, 150ms tail, release. Block RX during TX.
6. `app_main`: IDLE/RX/TX state machine. Stubs left empty: text transport, CAT tune, mode set, CW TX/RX (reuses same DAC + Goertzel path).
7. `calib`: software gain + pot sweep, loopback test, log stats.

## Bring-up order
- Continuity check + PTT alone. No audio yet.
- RX only: play fldigi DominoEX11 into ESP32, check log decode.
- TX only: ESP32 tones into fldigi, check clean spectrum + text.
- QSO USB, 5W max, dummy load first, then antenna.
- Measure levels with scope at each step.

## Future hooks
- Text I/O: USB-CDC queue already isolated. Add BLE/WiFi later.
- CAT: stub UART for freq/mode set. Manual tune for now.
- CW: same DAC for keyed sine, same ADC + single Goertzel ~700Hz. Add key line later.
- Codec upgrade path: keep `audio_io` interface abstract. Swap internal DAC/ADC for I2S PCM5102A + PCM1808 without touching modem.

## BOM minimal
- ESP32-WROOM32 devkit (have)
- G106 (have)
- miniDIN8 plug/cable, 2x 600:600 transformers, PC817, 2x 10k trimmers, R/C assortment, ferrite, perfboard/box
