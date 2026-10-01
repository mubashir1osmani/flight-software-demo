// builds xbee 0x10 transmit request frames and parses 0x90 receive packets.
// assumes the radio is in api mode 1 (ap=1), so no byte escaping.
#ifndef CANSAT_COMMS_XBEE_FRAME_HPP
#define CANSAT_COMMS_XBEE_FRAME_HPP

#include <cstddef>
#include <cstdint>

namespace cansat {
namespace xbee {

#ifndef CANSAT_XBEE_MAX_PAYLOAD
// should match the radio's np setting
#define CANSAT_XBEE_MAX_PAYLOAD 100
#endif

constexpr uint8_t kStartDelimiter = 0x7E;
constexpr uint8_t kFrameTypeTransmitRequest = 0x10;
constexpr uint8_t kFrameTypeReceivePacket = 0x90;

constexpr uint64_t kBroadcastAddress = 0x000000000000FFFFULL;
constexpr uint16_t kReservedAddress16 = 0xFFFE;

constexpr size_t kMaxPayloadSize = CANSAT_XBEE_MAX_PAYLOAD;

constexpr size_t kFramingBytes = 4;  // delimiter, 2 length bytes, checksum
// bytes counted by the length field before the payload starts
constexpr size_t kTransmitRequestHeaderBytes = 14;  // type, id, dest64, 0xfffe, radius, options
constexpr size_t kReceivePacketHeaderBytes = 12;    // type, src64, reserved, options

// largest length field the parser will accept for any frame type
constexpr size_t kMaxApiFrameDataLength = kTransmitRequestHeaderBytes + kMaxPayloadSize;

// biggest frame we can build, use this to size the tx buffer
constexpr size_t kMaxTransmitFrameSize =
    kFramingBytes + kTransmitRequestHeaderBytes + kMaxPayloadSize;

enum class Status : uint8_t {
  Ok = 0,
  NullArgument,
  PayloadTooLarge,
  BufferTooSmall,
  Incomplete,  // need more bytes
  BadStartDelimiter,
  BadLength,
  BadChecksum,
  UnexpectedFrameType
};

const char* to_string(Status s);

// frame_data starts at the frame type byte and runs to the end of the payload
uint8_t compute_checksum(const uint8_t* frame_data, size_t length);
bool verify_checksum(const uint8_t* frame_data, size_t length, uint8_t checksum);

struct TransmitRequest {
  uint64_t destination = kBroadcastAddress;  // defaults to broadcast
  uint8_t frame_id = 0;  // 0 means the radio won't send a 0x8b status back
  uint8_t broadcast_radius = 0;
  uint8_t options = 0;
};

// out is not modified if this fails
Status build_transmit_request(const TransmitRequest& request,
                              const uint8_t* payload, size_t payload_len,
                              uint8_t* out, size_t out_capacity,
                              size_t* out_len);

struct ReceivePacket {
  uint64_t source_address = 0;
  uint16_t reserved = 0;
  uint8_t receive_options = 0;
  const uint8_t* payload = nullptr;  // points into the input buffer
  size_t payload_len = 0;
  size_t frame_len = 0;  // bytes to consume from the input
};

// parses the frame at the start of data. out is only filled in on ok. for
// unexpectedframetype (like a 0x8b status frame) only frame_len is set so the
// caller can skip it.
Status parse_receive_packet(const uint8_t* data, size_t len, ReceivePacket* out);

}  // namespace xbee
}  // namespace cansat

#endif  // CANSAT_COMMS_XBEE_FRAME_HPP
