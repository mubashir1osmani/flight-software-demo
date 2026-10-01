#ifndef CANSAT_COMMS_MESSAGES_HPP
#define CANSAT_COMMS_MESSAGES_HPP

#include <cstdint>

namespace cansat {

// ground -> cansat
struct CommandMechanism {
  uint8_t mechanism_id;
  uint8_t value;
};

// ground -> cansat
struct CommandTelemetry {
  bool is_on;
};

// cansat -> ground
struct OutTelemetry {
  uint32_t seconds_since_epoch;
  uint16_t mechanisms_deployed_flags;
};

}  // namespace cansat

#endif  // CANSAT_COMMS_MESSAGES_HPP
