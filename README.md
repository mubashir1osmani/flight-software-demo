# cansat comms

xbee framing + payload encoding for the cansat flight software.

- `xbee_frame` builds 0x10 transmit requests and parses 0x90 receive packets
- `payload_codec` turns `OutTelemetry` into bytes and bytes into `CommandMechanism` / `CommandTelemetry`

no uart code in here, everything works on byte arrays so it can be tested on a laptop.

## build

```
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

uses system gtest if it's installed, otherwise cmake downloads it.

## payload format

first byte is the message id, then the fields in struct order. multi byte fields are big endian.

| id | message | bytes |
|----|---------|-------|
| 0x01 | CommandMechanism | `01 mechanism_id value` |
| 0x02 | CommandTelemetry | `02 is_on` (must be 0 or 1) |
| 0x03 | OutTelemetry | `03 seconds(4) flags(2)` |

e.g. `OutTelemetry{0x01020304, 0xA1B2}` -> `03 01 02 03 04 A1 B2`

the decoder rejects wrong lengths (including extra bytes), unknown ids and an `is_on` that isn't 0/1.

## notes

- assumes the xbee is in api mode 1 (AP=1), so no escaping
- no heap (no new/malloc/vector), caller passes in all the buffers. max sizes are constants (`kMaxTransmitFrameSize`, `kMaxMessageSize`)
- everything returns a status enum, bad input never crashes
- fields are written byte by byte instead of memcpy'ing structs so padding/endianness don't matter
- the parser returns `UnexpectedFrameType` + `frame_len` for other valid frames (like 0x8B tx status) so they can be skipped
- payload from the parser points into the rx buffer, it isn't copied
- max payload is 100 bytes, change with `-DCANSAT_XBEE_MAX_PAYLOAD=N`
