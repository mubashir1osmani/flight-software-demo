#include "cansat_comms/payload_codec.hpp"

namespace cansat {

namespace {

// message id as the raw byte that goes on the wire
constexpr uint8_t type_byte(MessageType t) { return static_cast<uint8_t>(t); }

// shared check for every serialize call
CodecStatus check_out(const uint8_t* out, size_t capacity, size_t needed) {
  if (out == nullptr) return CodecStatus::NullArgument;
  if (capacity < needed) return CodecStatus::BufferTooSmall;
  return CodecStatus::Ok;
}

}  // namespace

const char* to_string(CodecStatus s) {
  switch (s) {
    case CodecStatus::Ok: return "Ok";
    case CodecStatus::NullArgument: return "NullArgument";
    case CodecStatus::BufferTooSmall: return "BufferTooSmall";
    case CodecStatus::Empty: return "Empty";
    case CodecStatus::UnknownType: return "UnknownType";
    case CodecStatus::WrongLength: return "WrongLength";
    case CodecStatus::InvalidValue: return "InvalidValue";
  }
  return "Unknown";
}

CodecStatus serialize(const OutTelemetry& msg, uint8_t* out, size_t capacity,
                      size_t* out_len) {
  const CodecStatus st = check_out(out, capacity, kOutTelemetrySize);
  if (st != CodecStatus::Ok) return st;

  out[0] = type_byte(MessageType::OutTelemetry);
  // seconds, high byte first
  out[1] = static_cast<uint8_t>(msg.seconds_since_epoch >> 24);
  out[2] = static_cast<uint8_t>(msg.seconds_since_epoch >> 16);
  out[3] = static_cast<uint8_t>(msg.seconds_since_epoch >> 8);
  out[4] = static_cast<uint8_t>(msg.seconds_since_epoch);
  // deployed flags, high byte first
  out[5] = static_cast<uint8_t>(msg.mechanisms_deployed_flags >> 8);
  out[6] = static_cast<uint8_t>(msg.mechanisms_deployed_flags);
  if (out_len != nullptr) *out_len = kOutTelemetrySize;
  return CodecStatus::Ok;
}

CodecStatus serialize(const CommandMechanism& msg, uint8_t* out, size_t capacity,
                      size_t* out_len) {
  const CodecStatus st = check_out(out, capacity, kCommandMechanismSize);
  if (st != CodecStatus::Ok) return st;

  out[0] = type_byte(MessageType::CommandMechanism);
  out[1] = msg.mechanism_id;
  out[2] = msg.value;
  if (out_len != nullptr) *out_len = kCommandMechanismSize;
  return CodecStatus::Ok;
}

CodecStatus serialize(const CommandTelemetry& msg, uint8_t* out, size_t capacity,
                      size_t* out_len) {
  const CodecStatus st = check_out(out, capacity, kCommandTelemetrySize);
  if (st != CodecStatus::Ok) return st;

  out[0] = type_byte(MessageType::CommandTelemetry);
  // always 0 or 1, never whatever bool happens to be in memory
  out[1] = msg.is_on ? 0x01 : 0x00;
  if (out_len != nullptr) *out_len = kCommandTelemetrySize;
  return CodecStatus::Ok;
}

CodecStatus deserialize_command(const uint8_t* payload, size_t len,
                                DecodedCommand* out) {
  if (payload == nullptr || out == nullptr) return CodecStatus::NullArgument;
  // need at least the id byte
  if (len == 0) return CodecStatus::Empty;

  // first byte says which message this is
  switch (payload[0]) {
    case type_byte(MessageType::CommandMechanism): {
      // exact length only, extra bytes mean something is wrong
      if (len != kCommandMechanismSize) return CodecStatus::WrongLength;
      out->type = MessageType::CommandMechanism;
      out->mechanism.mechanism_id = payload[1];
      out->mechanism.value = payload[2];
      return CodecStatus::Ok;
    }
    case type_byte(MessageType::CommandTelemetry): {
      if (len != kCommandTelemetrySize) return CodecStatus::WrongLength;
      // only accept 0 or 1, anything else is probably corrupted
      if (payload[1] > 0x01) return CodecStatus::InvalidValue;
      out->type = MessageType::CommandTelemetry;
      out->telemetry.is_on = payload[1] == 0x01;
      return CodecStatus::Ok;
    }
    default:
      // includes 0x03, since the cansat never receives telemetry
      return CodecStatus::UnknownType;
  }
}

CodecStatus deserialize(const uint8_t* payload, size_t len, OutTelemetry* out) {
  if (payload == nullptr || out == nullptr) return CodecStatus::NullArgument;
  if (len == 0) return CodecStatus::Empty;
  if (payload[0] != type_byte(MessageType::OutTelemetry)) {
    return CodecStatus::UnknownType;
  }
  if (len != kOutTelemetrySize) return CodecStatus::WrongLength;

  // rebuild seconds from 4 bytes, high byte first
  out->seconds_since_epoch = (static_cast<uint32_t>(payload[1]) << 24) |
                             (static_cast<uint32_t>(payload[2]) << 16) |
                             (static_cast<uint32_t>(payload[3]) << 8) |
                             static_cast<uint32_t>(payload[4]);
  // rebuild flags from 2 bytes
  out->mechanisms_deployed_flags =
      static_cast<uint16_t>((payload[5] << 8) | payload[6]);
  return CodecStatus::Ok;
}

}  // namespace cansat
