# dominoBrick BLE API

Transport for the DominoEX11 modem. NimBLE peripheral, open link (no pairing).
Device name: `dominoBrick`.

## Discovery / advertising

Scan for the service UUID, not the name. The advertising packet carries the
complete 128-bit service UUID list (`d0b10000-bbaa-9988-7766-554433221100`)
plus flags; the complete local name `dominoBrick` and TX power go in the scan
response (a 128-bit UUID and the full name do not fit in the 31-byte
advertising packet together). Filter on the service UUID to identify
dominoBrick devices.

Service UUID: `d0b10000-bbaa-9988-7766-554433221100`

| Characteristic | UUID suffix | Properties | Direction |
|---|---|---|---|
| TX_DATA | `d0b10001-…` | Write, WriteNR | client → modem |
| TX_PROGRESS | `d0b10002-…` | Notify, Read | modem → client |
| SEC_MSG | `d0b10003-…` | Write, WriteNR, Read | client → modem |
| RX_PRIMARY | `d0b10004-…` | Notify, Read | modem → client |
| RX_SECONDARY | `d0b10005-…` | Notify, Read | modem → client |
| FREQ | `d0b10006-…` | Write, WriteNR, Read, Notify | both |

(Full UUIDs: replace `d0b10000` with the suffix above, rest identical.)

## TX_DATA — send text

Streaming append. Every write appends its bytes verbatim to the TX buffer, in
order. No end-of-message marker. Single characters work: type-and-send
forwards each keystroke immediately.

- Writes are atomic: if the write does not fit the free buffer (1024 B total),
  the whole write is rejected and nothing is appended. Write-with-response
  fails with ATT error `0x80` (insufficient resources) — retry later.
  WriteNR drops silently when full — prefer Write-with-response.
- Any successful write keys the transmitter (or keeps the session keyed).
  The session unkeys 3 s after the last write once the buffer drains, sending
  the standard `\r EOT \r` + idle trailer. Back-to-back writes share one
  keyed session (no re-preamble).
- Payload is raw bytes; the modem encodes them as primary-channel DominoEX.
  Framing (`\r STX \r` … `\r EOT \r`) is added by the modem, not the client.

## TX_PROGRESS — progress reporting

Echo protocol. After each primary payload character is actually aired, the
modem notifies 1 byte: that character. The client tracks progress by matching
the echo stream against its sent stream in order (echoes arrive in airing
order, so repeated characters resolve by position).

- Framing bytes and secondary filler characters are never echoed — only
  primary payload characters the client sent.
- `Read` returns the last echoed character (poll fallback, 1 byte).
- Resync limit: after a reconnect only the last character is known, not its
  position. Pause, drain, and restart the stream after reconnecting.

## SEC_MSG — secondary text

Write replaces the secondary (fldigi-style) text, max 127 bytes (longer
writes are truncated). The cycle restarts from the beginning on every write.
No notifications. `Read` returns the current text.

The secondary text is aired on the secondary channel whenever the primary
buffer is empty and the session is held open (i.e. while typing). It is also
what fills airtime between keystrokes.

## RX_PRIMARY / RX_SECONDARY — received text

Each decoded character produces one 1-byte notification on the matching
channel characteristic. `Read` returns the most recently notified chunk
(currently the last character; cache holds up to 240 B for future batching).

## FREQ — operating frequency

4-byte little-endian `uint32`, frequency in Hz. Write sets the rig's operating
frequency via CAT: the modem first sets USB (DominoEX mode), then the
frequency, then verifies by readback. `Read` returns the current value.

- `uint32` covers the whole HF/VHF range with margin (expected use ≤ 450 MHz).
- Writes must be exactly 4 bytes, otherwise rejected (ATT invalid length).
  Zero or > 450 MHz is rejected (ATT unlikely).
- Notify: the modem notifies whenever the frequency changes, including
  changes made directly on the radio knobs. Subscribe + `Read` once on
  connect to get the initial value.
- Status: implemented (`main/cat.c`, 1 s poll + snoop).

## Link details

- Open, unencrypted link (JustWorks, no bonding) for v1.
- Preferred MTU 240; long notifies are chunked to MTU-3 by the server.
- Connection parameters requested on connect: interval 15–30 ms, latency 0,
  supervision timeout 4 s (central may override). At 15–30 ms the link carries
  many packets per event — 11 writes/s + 11 notifies/s is nowhere near the
  limit; worst-case echo latency is about one interval.
- One connection at a time. Disconnects do not disturb the modem: the
  session closes via the normal 3 s timeout, RX/echo caches stay readable.
- Cross-core design: modem DSP runs on core 1, NimBLE host on core 0. All
  BLE↔modem exchange uses FreeRTOS queues and single-byte mailboxes; GATT
  callbacks never block the modem.
