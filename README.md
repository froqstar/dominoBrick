# dominoBrick

ESP32 (classic WROOM32) DominoEX11 modem for the Xiegu G106. It sits between
your phone/laptop and the rig's ACC port: you send text over BLE, it transmits
DominoEX11 audio; it decodes received audio and sends the text back over BLE.

No beacons, no auto-transmit. The modem only keys when you give it something
to send.

## Hardware

- ESP32-WROOM32 (classic: needs the built-in DACs on GPIO25/26 plus BLE).
  S2 has no Bluetooth, S3/C3/C6 have no DAC — stick with the classic ESP32.
- G106 ACC port: AF in/out through 600:600 isolation transformers and level
  trimmers, PTT through a PC817 opto (GPIO4, active low). RC lowpass (~3 kHz)
  on both audio directions.
- See `PLAN.md` for the full wiring sketch and bring-up order
  (continuity → PTT → RX → TX → on-air, 5 W max, dummy load first).

## Using it

1. Power up, connect the ACC cable, open your BLE app and connect to
   **`dominoBrick`**.
2. Subscribe to `TX_PROGRESS` (`d0b1…0002`) and `RX_PRIMARY` (`d0b1…0004`).
3. Write your text to `TX_DATA` (`d0b1…0001`). Every write is appended to the
   transmit buffer and aired; the rig keys automatically and unkeys 3 s after
   the buffer drains.
4. Watch `TX_PROGRESS`: it echoes each character as it goes on air, in order —
   match it against what you sent to see progress.
5. Received text arrives as notifications on `RX_PRIMARY` (secondary channel
   on `RX_SECONDARY`, `d0b1…0005`). Set the secondary filler text via
   `SEC_MSG` (`d0b1…0003`).

Full characteristic contract, edge cases and session timing: [`BLE_API.md`](BLE_API.md).

### Quick test without a phone

Press the **BOOT** button (GPIO0) on the devkit. It queues `DOMINOBRICK test`
exactly as if it arrived over BLE — PTT keys, audio goes out, progress
echoes. Use this to verify TX audio into fldigi before debugging your app.

## Build & flash

Requires ESP-IDF v5.3.2 at `~/esp/esp-idf`, target ESP32.

```
. ~/esp/esp-idf/export.sh && idf.py build
idf.py -p /dev/ttyUSB0 flash
```

Bluetooth (NimBLE) is enabled in `sdkconfig.defaults`; the modem DSP tasks
run pinned to core 1, the BLE host on core 0.

## Host tests (no hardware needed)

```
gcc -DHOST_TEST -O2 -o /tmp/loopback test/loopback.c main/dominoex.c main/dominovar.c main/dsp_goertzel.c -Imain -lm && /tmp/loopback
gcc -DHOST_TEST -O2 -o /tmp/roundtrip test/roundtrip.c main/dominoex.c main/dominovar.c -Imain && /tmp/roundtrip
```

- `loopback` checks substring match (`strstr`), not exact equality: the first
  1–2 characters after key-up are legitimately lost to receiver acquisition.
- `roundtrip` verifies all 512 varicode table entries, including the known
  fldigi quirk where secondary `{` decodes as `}` (kept for interop).

## Layout

- `main/app_main.c` — RX/TX tasks, session/PTT logic, BOOT button handler
- `main/ble_server.c` — NimBLE service, GATT table, notify path
- `main/dominoex.c`, `main/dominovar.c` — verbatim fldigi DominoEX codec
  (do not hand-edit the tables)
- `main/dsp_goertzel.c` — complex-differential detector bank + AFC
- `main/audio_io.c`, `main/tx_nco.c`, `main/ptt.c` — DAC/ADC, NCO, PTT
- `test/` — host DSP tests (`probe.c` is a one-off diagnostic)
- `AGENTS.md` — contributor notes: protocol invariants and hard-earned DSP
  constraints. Read it before touching modem code.
