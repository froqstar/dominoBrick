# dominoBrick — ESP32 (WROOM32) DominoEX11 modem for Xiegu G106

## Build & test (exact commands)

- Firmware needs ESP-IDF v5.3.2 at `~/esp/esp-idf`, target ESP32 (classic WROOM32, dual-core + BT Classic/BLE). Always source first, then build with a long timeout (first configure is slow):
  `. ~/esp/esp-idf/export.sh && idf.py build`
- Host tests need no IDF. `HOST_TEST` selects stub pin defines in `main/config.h`:
  `gcc -DHOST_TEST -O2 -o /tmp/loopback test/loopback.c main/dominoex.c main/dominovar.c main/dsp_goertzel.c -Imain -lm && /tmp/loopback`
  `gcc -DHOST_TEST -O2 -o /tmp/roundtrip test/roundtrip.c main/dominoex.c main/dominovar.c -Imain && /tmp/roundtrip`
- `test/probe.c` is a one-off Goertzel/AFC diagnostic, not part of the suite.

## Non-obvious test semantics

- Loopback asserts `strstr(got, msg)` (full message as contiguous substring), NOT exact equality. First 1–2 chars after key-up are legitimately lost to acquisition — that is normal async-IFK behavior, not a bug. If you "fix" the assert to exact-match, you will chase ghosts.
- Loopback runs with +35 Hz offset, 211-sample skew, and noise baked in (`F_OFF_HZ`, `PRE_SAMPLES`, `NOISE_AMPL`). A change that passes clean-channel only is not done.

## Protocol: this is verbatim fldigi DominoEX, do not redesign

- `main/dominovar.c` is byte-exact from fldigi `src/dominoex/dominovar.cxx` (GPLv3 — that license now covers this repo). Never hand-edit the tables. The roundtrip test verifies all 512 entries; trust it over intuition.
- Known fldigi quirk, also verified: secondary `{` encodes to a code that decodes as `}`. It is in the test as an explicit allowlist. Do not "fix" it — interop means matching fldigi bug-for-bug.
- Wire format: 1 nibble/symbol, TX step `(prev + 2 + nibble) % 18`, tone centers `(tone+0.5)*spacing` (`TONE_BASE_HZ` half-bin macro — do not "simplify" to integer bins).
- NimBLE `BLE_UUID128_INIT` takes bytes **little-endian** (LSB first, full reverse of the canonical string — see `ble_uuid_to_str` printing `value[15]` first). For `d0b1SSSS-bbaa-9988-7766-554433221100` that is `(0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,SS,SS,0xb1,0xd0)`. A mixed-order init silently advertises a wrong UUID: name-based scanners still find the brick but service-UUID filters (CDM, our app) never match, and GATT lookups fail too.
- RX framing: nibble with MSB clear completes the previous char (see `decodeDomino` logic in `dominoex_rx_nibble`); `0x100` flag = secondary channel, `-1` = invalid, drop silently.
- TX sequence per key-up: 1× secondary NUL → `\r STX \r` → message → `\r EOT \r` → 4× idle, single PTT assertion. No diddle preamble (breaks interop), no per-char PTT (breaks receiver sync).
- TX sessions stay keyed while `typing_flag` is set (`domino_tx_set_typing`, future BLE/USB feeds `domino_tx_put`): primary whenever queued, else secondary cycling from `sec_text` (128 B buffer, fldigi's own secondary text is unbounded). Unkey only when flag is clear *and* the queue is drained. No auto-beacon: the modem never keys on its own.

## DSP architecture (hard-earned, do not regress)

- Data path is **complex-differential** (`goertzel_bins_wide` + `ifk_detect_wide`): correlate previous/current spectra, pick best jump. Never revert to argmax-tone-then-difference — it flips randomly at fractional-bin offsets and looks deceptively healthy in logs.
- Detector bank is **36 bins** (`NBANK`, ±9 around the 18 tones), fldigi-style. Required because AFC acquire can only resolve the offset up to whole-bin aliases on narrowband signals. Narrowing it back to 18 reintroduces edge-tone loss.
- `ifk_detect_wide` correlates forward pairs **and** wrapped pairs (`j = i+k-18`). The wrap half is load-bearing: large steps (nibbles 9, 15) are invisible without it.
- AFC is acquire-then-freeze (`afc_acquire_wide` = total-energy over 8 symbols; peak-picking aliases, do not use). No per-symbol AFC probe — it dithers between equivalent (tone, offset) locks and corrupts diffs. Re-acquire only after 8 consecutive low-confidence symbols.
- Symbol timing is early-late on three windows with hold-on-clean-lock (`co > 10` + small imbalance → `corr = 0`) and hold-on-silence (no window above `3.0` → `corr = 0`, never jump on flat noise). During steady tones timing is unobservable; any voter that corrects every symbol random-walks off the grid.
- RX noise handling is implicit, there is no squelch gate: `tone_detect_wide` best/second-bin ratio (`co < 3.0` ≈ no tone, timing only), `ifk_detect_wide` best/runner-up ratio (`conf < 4.0` = bad symbol), 8 consecutive bad symbols trigger AFC re-acquire (first symbol after acquire never decoded), and only MSB-clear nibbles complete chars (invalid codes dropped). Noise still emits occasional garbage chars, same as fldigi. A future squelch could suppress RX notify while the bad streak is high — see `rx_task` in `app_main.c`.

## Android app (`android/`)

- Stack: Kotlin, coroutines + Flow, Compose, Kable, no DI. minSdk 33, targetSdk 36, compileSdk 37. Versions: Gradle 9.7.1, AGP 9.4.0, Kotlin 2.4.20, Compose BOM 2026.09.00, Kable `kable-core` 0.44.3, coroutines 1.11.0. Only JDK on this box is 25 (`/usr/lib/jvm/java-25-openjdk`) — do not downgrade, modern AGP requires it.
- Build / install (long timeout, dependency downloads on first run):
  `export JAVA_HOME=/usr/lib/jvm/java-25-openjdk ANDROID_HOME=~/Android/Sdk && android/gradlew :app:assembleDebug`
  `adb install -r android/app/build/outputs/apk/debug/app-debug.apk`
  Always launch after install: `adb shell am start -n com.dominobrick.app/.MainActivity`
- SDK notes: platforms `android-34`, `android-36`, `android-37.0` installed; `cmdline-tools/latest` was added for `sdkmanager`. `android/local.properties` pins `sdk.dir=/home/froqstar/Android/Sdk`.
- AGP 9 quirks (do not regress): no `kotlin.android` plugin — Kotlin support is built into AGP 9, only `org.jetbrains.kotlin.plugin.compose` is applied. BOM 2026.09 requires compileSdk 37. Kable 0.44 uses stdlib `kotlin.uuid.Uuid` (no `kable.Uuid`), `characteristicOf(Uuid.parse(..), Uuid.parse(..))`, `Peripheral(identifier)` / `Peripheral(bluetoothDevice)`, `AndroidPeripheral.requestMtu`.
- Architecture: `DominoApp` holds singletons (`DeviceStore`, `DominoBleClient`, `ChatRepository`), no DI. Chat streams per-keystroke writes to TX_DATA (WriteWithResponse, 0x80 retry); TX_PROGRESS echoes mark SENT chars AIRED in order; RX notifies append RECEIVED; secondary channel buffer snapshots into `author` (heuristic later). MAC only persisted (`SharedPreferences` `bonded_mac`).
- Presence + link keepalive: Settings pairs via CDM `associate()` (service-UUID filter, system picker, no app list, no SCAN/location perms). `CompanionPairing.observePresence` arms `startObservingDevicePresence` by association id (needs `REQUEST_OBSERVE_COMPANION_DEVICE_PRESENCE`; `setUuid` path is classic-BT/automotive only — wrong for us). `DominoCompanionService` (`BIND_COMPANION_DEVICE_SERVICE` + `android.companion.CompanionDeviceService` filter) starts `DominoLinkService` on appear. FGS type `connectedDevice` + matching permission; retry loop 5 s → 60 s backoff; stops on forget/disconnect/notification Stop. `AssociationInfo.getDeviceMacAddressAsString` is `@hide` — use `getDeviceMacAddress().toString()`.
- FREQ (`d0b10006`, 4-byte LE Hz, Read + Notify + Write): app subscribes + reads once on connect into `DominoBleClient.freqHz`; TopBar dropdown shows live freq (favorite name over it on exact match, else plain freq), selecting a favorite writes it to the brick. FREQ collectors are `runCatching`-isolated so a missing char can't kill the link. Firmware side not yet implemented — until then demo mode injects per-station freqs via `injectFreqForDemo`. Favorites live in SharedPreferences (`favorite_freqs` as `id|name|freqHz` set).

## Hardware constraints baked into the code

- Classic ESP32-WROOM32 was chosen: 2× 8-bit DAC (GPIO25/26) + BT Classic + BLE. S2 lacks BT, S3/C3/C6 lack DAC — don't retarget without adding an I2S codec.
- `audio_io.c` uses `dac_continuous` DMA at 8 kHz + `adc_oneshot` (IDF v5 API). DAC clock source must be APLL (`DAC_DIGI_CLK_SRC_APLL`): the D2PLL path bottoms out at 19.6 kHz and aborts init at 8 kHz. TX uses **async mode, started once at boot and never stopped**: `start_async_writing` links the full descriptor ring, a feeder task refills completed descs from an app-side FIFO (silence `128` when idle), `audio_tx_stream` appends with backpressure, `audio_tx_drain` + 4-desc pipeline flush before unkey. Never use sync `dac_continuous_write`: starting the engine with a partial chain or restarting it after stop wedges EOF completion permanently on this chip/IDF (pool drains, every write times out, and the old `ESP_ERROR_CHECK` on it rebooted the board). Never revert to per-sample `dac_oneshot` + `vTaskDelay` — the tick can't pace 125 µs (0-tick bug) and a spin-loop starves the BT stack. RX sampling is a 125 µs `esp_timer` pushing single `adc_oneshot` reads with timestamps into a queue; `rx_resample.c` interpolates them onto an exact 8 kHz grid. Never batch 8 ADC reads per 1 ms tick — that samples in bursts with gaps (every-8-steps jumps, `conf` pinned ~1) while averaging exactly 8 kHz, which looks healthy and misleads. Never run ADC continuous DMA alongside DAC continuous DMA (shared I2S0, aborts in `adc_continuous_new_handle`); start the RX sampler only after BT init (`audio_rx_start` — firing ADC reads through RF cal silently resets the board). The symbol math (`SAMPLES_PER_SYMBOL` = 743) depends on uniform spacing.
