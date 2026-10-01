// payload format: [message id][fields in order], big endian, no padding.
//   0x01 CommandMechanism  id, mechanism_id, value               3 bytes
//   0x02 CommandTelemetry  id, is_on (0 or 1)                    2 bytes
//   0x03 OutTelemetry      id, seconds (u32), deployed flags (u16)  7 bytes
#ifndef CANSAT_COMMS_PAYLOAD_CODEC_HPP
#define CANSAT_COMMS_PAYLOAD_CODEC_HPP

#include <cstddef>
#include <cstdint>

#include "cansat_comms/messages.hpp"

namespace cansat {

enum class MessageType : uint8_t {
  CommandMechanism = 0x01,
  CommandTelemetry = 0x02,
  OutTelemetry = 0x03,
};

constexpr size_t kCommandMechanismSize = 3;
constexpr size_t kCommandTelemetrySize = 2;
constexpr size_t kOutTelemetrySize = 7;
// biggest message, use this to size payload buffers
constexpr size_t kMaxMessageSize = kOutTelemetrySize;

enum class CodecStatus : uint8_t {
  Ok = 0,
  NullArgument,
  BufferTooSmall,
  Empty,
  UnknownType,
  WrongLength,
  InvalidValue  // e.g. is_on byte that isn't 0 or 1
};

const char* to_string(CodecStatus s);

CodecStatus serialize(const OutTelemetry& msg, uint8_t* out, size_t capacity,
                      size_t* out_len);

// only the field matching `type` is meaningful
struct DecodedCommand {
  MessageType type = MessageType::CommandMechanism;
  CommandMechanism mechanism{0, 0};
  CommandTelemetry telemetry{false};
};

// decodes a command from the ground. out is only written on ok.
CodecStatus deserialize_command(const uint8_t* payload, size_t len,
                                DecodedCommand* out);

CodecStatus serialize(const CommandMechanism& msg, uint8_t* out, size_t capacity,
                      size_t* out_len);
CodecStatus serialize(const CommandTelemetry& msg, uint8_t* out, size_t capacity,
                      size_t* out_len);
// ground station side
CodecStatus deserialize(const uint8_t* payload, size_t len, OutTelemetry* out);

}  // namespace cansat

#endif  // CANSAT_COMMS_PAYLOAD_CODEC_HPP
