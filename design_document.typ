// Build: typst compile docs/design_document.typ
#set document(title: "CanSat Communications Library: Design Document")
#set page(paper: "us-letter", margin: (x: 0.75in, y: 0.6in))
#set text(font: ("Helvetica Neue", "Helvetica", "Arial"), size: 9.3pt)
#set par(justify: true, leading: 0.5em, spacing: 0.75em)
#set list(indent: 0.4em, body-indent: 0.45em, spacing: 0.5em)
#show raw: set text(font: ("Menlo", "Courier New"), size: 8.6pt)
#show heading.where(level: 1): it => block(above: 1.0em, below: 0.55em,
  text(size: 11pt, weight: "bold", it.body))

#align(center)[
  #text(size: 15pt, weight: "bold")[CanSat Communications Library: Design Document] \
  #v(0.15em)
  #text(fill: gray.darken(20%))[XBee 0x10 / 0x90 framing and payload serialization for Flight Software]
]

= 1. Microcontroller suitability and avoiding dynamic allocation

The library targets a small Arm Cortex-M. It never uses the heap: no `new`, `delete`, `malloc`, STL containers, exceptions or RTTI. It builds cleanly with `-fno-exceptions -fno-rtti -Wconversion`.

- *Caller-owned, fixed-size buffers.* Every function writes into a buffer the caller passes in, along with its capacity. Compile-time constants (`kMaxTransmitFrameSize` = 118 B, `kMaxMessageSize` = 7 B) let those buffers be allocated statically. Worst-case RAM is known at link time, and nothing can fragment or run out of memory mid-flight.
- *Zero-copy receive.* `parse_receive_packet()` returns a pointer and length into the caller's receive buffer instead of copying the RF data. On transmit, the only copy is placing the payload into the frame, which can't be avoided.
- *Deterministic cost.* Every operation is a single bounded O(n) pass with no recursion or allocation. It is safe to call from a periodic task, and short enough to call from a UART RX handler.
- *No platform dependencies.* The library uses only `<cstdint>` and `<cstddef>`, with no I/O, OS or UART code. The same code runs on the desktop for unit tests and on the target.

*Tradeoffs.*
- The maximum payload is fixed at compile time (default 100 B, matching XBee `NP`), so buffers always reserve the worst case even when a frame is only 25 B.
- A receive view is valid only while the caller's buffer is unchanged, so the payload must be decoded before the buffer is reused.
- Pointer-and-length APIs are less convenient than `std::vector`, and every status code has to be checked.

These costs are worth it for bounded memory and no hidden failure paths.

= 2. Payload format

Byte 0 of every payload is a *message type ID*. The fields follow in declaration order, with no padding:

#{
show raw: set text(size: 7.6pt)
table(
  columns: (auto, auto, auto, 1fr),
  inset: (x: 5pt, y: 3pt),
  stroke: 0.4pt + gray,
  fill: (_, y) => if y == 0 { luma(230) },
  table.header([*ID*], [*Message*], [*Bytes*], [*Layout*]),
  [`0x01`], [CommandMechanism], [3], [`[01][mechanism_id u8][value u8]`],
  [`0x02`], [CommandTelemetry], [2], [`[02][is_on u8 : 0x00 or 0x01 only]`],
  [`0x03`], [OutTelemetry], [7], [`[03][seconds_since_epoch u32 BE][mechanisms_deployed_flags u16 BE]`],
)
}

- *Field sizes match the struct types exactly.* The uint32 seconds and uint16 flags are not narrowed, so no range is lost. A full telemetry payload is just 7 bytes. A `bool` takes one byte, which is simpler and clearer than bit-packing.
- *Big-endian (network order).* XBee already encodes its own length and address fields this way, so one convention covers the whole frame. Fields are written with explicit shifts rather than by `memcpy`-ing structs. The format therefore doesn't depend on compiler padding, `sizeof(bool)` or CPU endianness, and a little-endian Cortex-M and a ground-station PC produce identical bytes.
- *A 1-byte type ID first.* The receiver can tell messages apart before reading any fields, and each type gets its own exact length check. There is room for 252 more types. ID 0x00 is reserved, so an all-zero payload is rejected. `OutTelemetry` has its own ID even though the CanSat only sends it, so the ground station can decode it with the same code.
- *No checksum or version field in the payload.* The XBee frame checksum and the radio's MAC-level CRC already cover the data. A versioned message can be added later under a new type ID without breaking existing ones.

= 3. Handling malformed, incomplete or invalid input

Validation is *layered*. Each layer rejects what it can see and returns a specific `enum class` status. Every check reads only bytes already proven to exist, and output is written only on success. A caller can never act on a half-decoded message.

*Frame layer*, checked in this order:
+ Start delimiter: `BadStartDelimiter`.
+ Length field outside the range of any valid API frame: `BadLength`. A corrupt length can't cause an over-read or a long wait.
+ Fewer bytes than the declared frame: `Incomplete`.
+ Checksum mismatch: `BadChecksum`.
+ Frame type other than 0x90: `UnexpectedFrameType`, and `frame_len` is still reported.
+ A 0x90 frame too short for its header, or carrying more than the maximum payload: `BadLength`.

Checking the type after the checksum means that valid frames the radio sends on its own, such as 0x8B Transmit Status and 0x8A Modem Status, are recognised and skipped as whole frames. They are not mistaken for corruption.

*Payload layer:*
- Empty payload: `Empty`.
- Unknown ID: `UnknownType`.
- Too short, or with trailing bytes: `WrongLength`.
- A bool byte other than 0 or 1: `InvalidValue`.

Tests cover all of the following. None of them crash or are accepted:
- every truncation of a valid frame
- every single-bit flip in every byte
- wrong lengths and frame types
- null pointers
- thousands of pseudo-random buffers

*What Flight Software should do:*
- *`Incomplete`* is not an error. Keep the bytes and wait for more UART data. Use a timeout so a frame that stops arriving partway is eventually discarded.
- *`BadStartDelimiter`, `BadLength`, `BadChecksum`:* drop one byte and scan forward for the next 0x7E. A 0x7E that happens to appear inside data is rejected by the length and checksum checks.
- *`UnexpectedFrameType`:* consume `frame_len` bytes. Optionally pass 0x8B frames to delivery tracking.
- *A valid 0x90 frame whose payload fails to decode:* consume `frame_len` bytes and ignore the message.
- *Never actuate on a doubtful command.* Deploying a mechanism by mistake is worse than missing a command, because the ground station can resend it. Keep a counter for each error status and report the counters in telemetry, so the ground team can tell link noise from a software bug.
- Malformed input must never reset or stall the flight loop. Telemetry keeps going no matter what arrives on the receive path.
