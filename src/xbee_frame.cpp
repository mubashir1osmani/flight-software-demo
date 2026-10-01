#include "cansat_comms/xbee_frame.hpp"

namespace cansat {
namespace xbee {

namespace {

// write a 16 bit value high byte first
void put_u16_be(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v);
}

// write a 64 bit address high byte first, like xbee expects
void put_u64_be(uint8_t* p, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    p[i] = static_cast<uint8_t>(v >> (56 - 8 * i));
  }
}

// read a 64 bit address back out, high byte first
uint64_t get_u64_be(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) {
    v = (v << 8) | p[i];
  }
  return v;
}

}  // namespace

const char* to_string(Status s) {
  switch (s) {
    case Status::Ok: return "Ok";
    case Status::NullArgument: return "NullArgument";
    case Status::PayloadTooLarge: return "PayloadTooLarge";
    case Status::BufferTooSmall: return "BufferTooSmall";
    case Status::Incomplete: return "Incomplete";
    case Status::BadStartDelimiter: return "BadStartDelimiter";
    case Status::BadLength: return "BadLength";
    case Status::BadChecksum: return "BadChecksum";
    case Status::UnexpectedFrameType: return "UnexpectedFrameType";
  }
  return "Unknown";
}

uint8_t compute_checksum(const uint8_t* frame_data, size_t length) {
  // add up every byte, letting it wrap so only the low 8 bits are kept
  uint8_t sum = 0;
  for (size_t i = 0; frame_data != nullptr && i < length; ++i) {
    sum = static_cast<uint8_t>(sum + frame_data[i]);
  }
  // xbee checksum is 0xff minus that sum
  return static_cast<uint8_t>(0xFF - sum);
}

bool verify_checksum(const uint8_t* frame_data, size_t length, uint8_t checksum) {
  return compute_checksum(frame_data, length) == checksum;
}

Status build_transmit_request(const TransmitRequest& request,
                              const uint8_t* payload, size_t payload_len,
                              uint8_t* out, size_t out_capacity,
                              size_t* out_len) {
  // a null payload is fine only if there is nothing to send
  if (out == nullptr || (payload == nullptr && payload_len != 0)) {
    return Status::NullArgument;
  }
  if (payload_len > kMaxPayloadSize) {
    return Status::PayloadTooLarge;
  }

  // length field covers frame type through the end of the payload
  const size_t length_field = kTransmitRequestHeaderBytes + payload_len;
  // plus the delimiter, the two length bytes and the checksum
  const size_t total = kFramingBytes + length_field;
  // make sure it all fits before writing anything
  if (out_capacity < total) {
    return Status::BufferTooSmall;
  }

  out[0] = kStartDelimiter;
  put_u16_be(&out[1], static_cast<uint16_t>(length_field));
  out[3] = kFrameTypeTransmitRequest;
  out[4] = request.frame_id;
  put_u64_be(&out[5], request.destination);
  // 16 bit address unknown, so use 0xfffe
  put_u16_be(&out[13], kReservedAddress16);
  out[15] = request.broadcast_radius;
  out[16] = request.options;
  // payload goes in as-is, this layer doesn't care what it means
  for (size_t i = 0; i < payload_len; ++i) {
    out[17 + i] = payload[i];
  }
  // checksum starts at the frame type byte (index 3)
  out[total - 1] = compute_checksum(&out[3], length_field);

  if (out_len != nullptr) {
    *out_len = total;
  }
  return Status::Ok;
}

Status parse_receive_packet(const uint8_t* data, size_t len, ReceivePacket* out) {
  if (data == nullptr || out == nullptr) {
    return Status::NullArgument;
  }
  // nothing has arrived yet
  if (len == 0) {
    return Status::Incomplete;
  }
  if (data[0] != kStartDelimiter) {
    return Status::BadStartDelimiter;
  }
  // can't read the length field until both bytes are here
  if (len < 3) {
    return Status::Incomplete;
  }

  const size_t length_field = (static_cast<size_t>(data[1]) << 8) | data[2];
  // reject lengths no frame could have, so a corrupt length doesn't make us
  // wait for bytes that will never come. this check is for any frame type,
  // the 0x90 specific one is further down so 0x8b/0x8a frames can be skipped.
  if (length_field < 1 || length_field > kMaxApiFrameDataLength) {
    return Status::BadLength;
  }

  // whole frame including delimiter, length and checksum
  const size_t total = kFramingBytes + length_field;
  if (len < total) {
    return Status::Incomplete;
  }

  if (!verify_checksum(&data[3], length_field, data[total - 1])) {
    return Status::BadChecksum;
  }
  // a valid frame, just not one we want. tell the caller how long it is so
  // they can skip over it.
  if (data[3] != kFrameTypeReceivePacket) {
    out->frame_len = total;
    return Status::UnexpectedFrameType;
  }
  // a 0x90 has to at least fit its header, and not go over our payload limit
  if (length_field < kReceivePacketHeaderBytes ||
      length_field > kReceivePacketHeaderBytes + kMaxPayloadSize) {
    return Status::BadLength;
  }

  out->source_address = get_u64_be(&data[4]);
  out->reserved = static_cast<uint16_t>((data[12] << 8) | data[13]);
  out->receive_options = data[14];
  out->payload_len = length_field - kReceivePacketHeaderBytes;
  // point at the payload inside the input buffer instead of copying it
  out->payload = out->payload_len != 0 ? &data[15] : nullptr;
  out->frame_len = total;
  return Status::Ok;
}

}  // namespace xbee
}  // namespace cansat
