#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

#include "cansat_comms/payload_codec.hpp"

using namespace cansat;


TEST(OutTelemetry, ExactBytesBigEndian) {
  const OutTelemetry msg{0x01020304u, 0xA1B2};
  uint8_t out[16];
  size_t n = 0;
  ASSERT_EQ(serialize(msg, out, sizeof(out), &n), CodecStatus::Ok);
  const uint8_t expected[] = {0x03, 0x01, 0x02, 0x03, 0x04, 0xA1, 0xB2};
  ASSERT_EQ(n, sizeof(expected));
  EXPECT_EQ(0, std::memcmp(out, expected, n));
}

TEST(OutTelemetry, ZeroAndMaxValues) {
  uint8_t out[7];
  size_t n = 0;
  ASSERT_EQ(serialize(OutTelemetry{0, 0}, out, sizeof(out), &n), CodecStatus::Ok);
  const uint8_t zero[] = {0x03, 0, 0, 0, 0, 0, 0};
  EXPECT_EQ(0, std::memcmp(out, zero, 7));

  ASSERT_EQ(serialize(OutTelemetry{0xFFFFFFFFu, 0xFFFF}, out, sizeof(out), &n),
            CodecStatus::Ok);
  const uint8_t max[] = {0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  EXPECT_EQ(0, std::memcmp(out, max, 7));
}

TEST(OutTelemetry, RoundTrip) {
  const OutTelemetry in{1'700'000'000u, 0x0005};
  uint8_t buf[kMaxMessageSize];
  size_t n = 0;
  ASSERT_EQ(serialize(in, buf, sizeof(buf), &n), CodecStatus::Ok);
  OutTelemetry back{};
  ASSERT_EQ(deserialize(buf, n, &back), CodecStatus::Ok);
  EXPECT_EQ(back.seconds_since_epoch, in.seconds_since_epoch);
  EXPECT_EQ(back.mechanisms_deployed_flags, in.mechanisms_deployed_flags);
}

TEST(OutTelemetry, BufferTooSmallLeavesBufferUntouched) {
  uint8_t out[6];
  std::memset(out, 0xEE, sizeof(out));
  EXPECT_EQ(serialize(OutTelemetry{1, 2}, out, sizeof(out), nullptr),
            CodecStatus::BufferTooSmall);
  for (uint8_t b : out) EXPECT_EQ(b, 0xEE);
}

TEST(OutTelemetry, ExactCapacityFits) {
  uint8_t out[kOutTelemetrySize];
  EXPECT_EQ(serialize(OutTelemetry{1, 2}, out, sizeof(out), nullptr), CodecStatus::Ok);
}

TEST(OutTelemetry, NullOutput) {
  EXPECT_EQ(serialize(OutTelemetry{1, 2}, nullptr, 16, nullptr), CodecStatus::NullArgument);
}

TEST(OutTelemetry, DeserializeRejectsBadInput) {
  OutTelemetry t{};
  const uint8_t good[] = {0x03, 0, 0, 0, 1, 0, 2};
  EXPECT_EQ(deserialize(good, 0, &t), CodecStatus::Empty);
  EXPECT_EQ(deserialize(good, 6, &t), CodecStatus::WrongLength);
  const uint8_t extra[] = {0x03, 0, 0, 0, 1, 0, 2, 9};
  EXPECT_EQ(deserialize(extra, sizeof(extra), &t), CodecStatus::WrongLength);
  const uint8_t wrong_type[] = {0x01, 0, 0, 0, 1, 0, 2};
  EXPECT_EQ(deserialize(wrong_type, sizeof(wrong_type), &t), CodecStatus::UnknownType);
  EXPECT_EQ(deserialize(nullptr, 7, &t), CodecStatus::NullArgument);
  EXPECT_EQ(deserialize(good, 7, nullptr), CodecStatus::NullArgument);
}


TEST(CommandMechanism, Decodes) {
  const uint8_t payload[] = {0x01, 0x07, 0xC8};
  DecodedCommand cmd;
  ASSERT_EQ(deserialize_command(payload, sizeof(payload), &cmd), CodecStatus::Ok);
  EXPECT_EQ(cmd.type, MessageType::CommandMechanism);
  EXPECT_EQ(cmd.mechanism.mechanism_id, 0x07);
  EXPECT_EQ(cmd.mechanism.value, 0xC8);
}

TEST(CommandMechanism, ExtremeValues) {
  const uint8_t payload[] = {0x01, 0x00, 0xFF};
  DecodedCommand cmd;
  ASSERT_EQ(deserialize_command(payload, sizeof(payload), &cmd), CodecStatus::Ok);
  EXPECT_EQ(cmd.mechanism.mechanism_id, 0);
  EXPECT_EQ(cmd.mechanism.value, 255);
}

TEST(CommandMechanism, TooShort) {
  DecodedCommand cmd;
  const uint8_t one[] = {0x01};
  const uint8_t two[] = {0x01, 0x07};
  EXPECT_EQ(deserialize_command(one, sizeof(one), &cmd), CodecStatus::WrongLength);
  EXPECT_EQ(deserialize_command(two, sizeof(two), &cmd), CodecStatus::WrongLength);
}

TEST(CommandMechanism, TrailingBytesRejected) {
  DecodedCommand cmd;
  const uint8_t p[] = {0x01, 0x07, 0x01, 0x00};
  EXPECT_EQ(deserialize_command(p, sizeof(p), &cmd), CodecStatus::WrongLength);
}

TEST(CommandMechanism, SerializeRoundTrip) {
  uint8_t buf[kMaxMessageSize];
  size_t n = 0;
  ASSERT_EQ(serialize(CommandMechanism{3, 9}, buf, sizeof(buf), &n), CodecStatus::Ok);
  EXPECT_EQ(n, kCommandMechanismSize);
  DecodedCommand cmd;
  ASSERT_EQ(deserialize_command(buf, n, &cmd), CodecStatus::Ok);
  EXPECT_EQ(cmd.mechanism.mechanism_id, 3);
  EXPECT_EQ(cmd.mechanism.value, 9);
}


TEST(CommandTelemetry, DecodesOn) {
  const uint8_t p[] = {0x02, 0x01};
  DecodedCommand cmd;
  ASSERT_EQ(deserialize_command(p, sizeof(p), &cmd), CodecStatus::Ok);
  EXPECT_EQ(cmd.type, MessageType::CommandTelemetry);
  EXPECT_TRUE(cmd.telemetry.is_on);
}

TEST(CommandTelemetry, DecodesOff) {
  const uint8_t p[] = {0x02, 0x00};
  DecodedCommand cmd;
  ASSERT_EQ(deserialize_command(p, sizeof(p), &cmd), CodecStatus::Ok);
  EXPECT_EQ(cmd.type, MessageType::CommandTelemetry);
  EXPECT_FALSE(cmd.telemetry.is_on);
}

TEST(CommandTelemetry, NonBooleanByteRejected) {
  DecodedCommand cmd;
  for (int v : {2, 3, 0x7F, 0x80, 0xFF}) {
    const uint8_t p[] = {0x02, static_cast<uint8_t>(v)};
    EXPECT_EQ(deserialize_command(p, sizeof(p), &cmd), CodecStatus::InvalidValue) << v;
  }
}

TEST(CommandTelemetry, WrongLength) {
  DecodedCommand cmd;
  const uint8_t short_p[] = {0x02};
  const uint8_t long_p[] = {0x02, 0x01, 0x01};
  EXPECT_EQ(deserialize_command(short_p, sizeof(short_p), &cmd), CodecStatus::WrongLength);
  EXPECT_EQ(deserialize_command(long_p, sizeof(long_p), &cmd), CodecStatus::WrongLength);
}

TEST(CommandTelemetry, SerializeRoundTrip) {
  for (bool on : {false, true}) {
    uint8_t buf[kMaxMessageSize];
    size_t n = 0;
    ASSERT_EQ(serialize(CommandTelemetry{on}, buf, sizeof(buf), &n), CodecStatus::Ok);
    EXPECT_EQ(n, kCommandTelemetrySize);
    DecodedCommand cmd;
    ASSERT_EQ(deserialize_command(buf, n, &cmd), CodecStatus::Ok);
    EXPECT_EQ(cmd.telemetry.is_on, on);
  }
}


TEST(Dispatch, DistinguishesTypesByFirstByte) {
  DecodedCommand cmd;
  const uint8_t a[] = {0x01, 0x01, 0x01};
  const uint8_t b[] = {0x02, 0x01};
  ASSERT_EQ(deserialize_command(a, sizeof(a), &cmd), CodecStatus::Ok);
  EXPECT_EQ(cmd.type, MessageType::CommandMechanism);
  ASSERT_EQ(deserialize_command(b, sizeof(b), &cmd), CodecStatus::Ok);
  EXPECT_EQ(cmd.type, MessageType::CommandTelemetry);
}

TEST(Dispatch, EmptyPayload) {
  DecodedCommand cmd;
  const uint8_t p[] = {0};
  EXPECT_EQ(deserialize_command(p, 0, &cmd), CodecStatus::Empty);
}

TEST(Dispatch, UnknownTypeIds) {
  DecodedCommand cmd;
  for (int id : {0x00, 0x04, 0x10, 0x7E, 0x90, 0xFF}) {
    const uint8_t p[] = {static_cast<uint8_t>(id), 0x01, 0x01};
    EXPECT_EQ(deserialize_command(p, sizeof(p), &cmd), CodecStatus::UnknownType) << id;
  }
}

TEST(Dispatch, OutTelemetryIsNotAnInboundCommand) {
  DecodedCommand cmd;
  const uint8_t p[] = {0x03, 0, 0, 0, 1, 0, 2};
  EXPECT_EQ(deserialize_command(p, sizeof(p), &cmd), CodecStatus::UnknownType);
}

TEST(Dispatch, NullArguments) {
  DecodedCommand cmd;
  const uint8_t p[] = {0x02, 0x01};
  EXPECT_EQ(deserialize_command(nullptr, 2, &cmd), CodecStatus::NullArgument);
  EXPECT_EQ(deserialize_command(p, sizeof(p), nullptr), CodecStatus::NullArgument);
}

TEST(Dispatch, OutputUntouchedOnFailure) {
  DecodedCommand cmd;
  cmd.type = MessageType::CommandTelemetry;
  cmd.telemetry.is_on = true;
  const uint8_t bad[] = {0x02, 0x05};
  EXPECT_NE(deserialize_command(bad, sizeof(bad), &cmd), CodecStatus::Ok);
  EXPECT_EQ(cmd.type, MessageType::CommandTelemetry);
  EXPECT_TRUE(cmd.telemetry.is_on);
}

TEST(Dispatch, GarbageNeverCrashes) {
  uint32_t state = 0xCAFEF00D;
  for (int iter = 0; iter < 5000; ++iter) {
    uint8_t buf[8];
    for (auto& b : buf) {
      state = state * 1664525u + 1013904223u;
      b = static_cast<uint8_t>(state >> 24);
    }
    DecodedCommand cmd;
    (void)deserialize_command(buf, static_cast<size_t>(iter) % (sizeof(buf) + 1), &cmd);
  }
}

TEST(CodecStatusStrings, AllNamed) {
  EXPECT_STREQ(to_string(CodecStatus::Ok), "Ok");
  EXPECT_STREQ(to_string(CodecStatus::InvalidValue), "InvalidValue");
}
