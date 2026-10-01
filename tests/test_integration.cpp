#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "cansat_comms/payload_codec.hpp"
#include "cansat_comms/xbee_frame.hpp"

using namespace cansat;

namespace {

// what the receiving radio would hand us for the same payload
std::vector<uint8_t> as_receive_packet(const uint8_t* tx, size_t tx_len) {
  const size_t payload_len = tx_len - (4 + xbee::kTransmitRequestHeaderBytes);
  std::vector<uint8_t> f = {0x7E, 0, 0, 0x90, 0, 0x13, 0xA2, 0, 0x40, 0x11,
                            0x22, 0x33, 0xFF, 0xFE, 0x01};
  f.insert(f.end(), tx + 17, tx + 17 + payload_len);
  const size_t len = f.size() - 3;
  f[1] = static_cast<uint8_t>(len >> 8);
  f[2] = static_cast<uint8_t>(len);
  f.push_back(xbee::compute_checksum(&f[3], f.size() - 3));
  return f;
}

}  // namespace

TEST(Integration, TelemetryGoesOutThroughBothLayers) {
  const OutTelemetry t{0x65000000u, 0x0003};
  uint8_t payload[kMaxMessageSize];
  size_t plen = 0;
  ASSERT_EQ(serialize(t, payload, sizeof(payload), &plen), CodecStatus::Ok);

  xbee::TransmitRequest req;
  req.destination = 0x0013A20012345678ULL;
  uint8_t frame[xbee::kMaxTransmitFrameSize];
  size_t flen = 0;
  ASSERT_EQ(xbee::build_transmit_request(req, payload, plen, frame, sizeof(frame), &flen),
            xbee::Status::Ok);
  EXPECT_EQ(flen, 4 + 14 + kOutTelemetrySize);

  const auto rx = as_receive_packet(frame, flen);
  xbee::ReceivePacket pkt;
  ASSERT_EQ(xbee::parse_receive_packet(rx.data(), rx.size(), &pkt), xbee::Status::Ok);
  OutTelemetry back{};
  ASSERT_EQ(deserialize(pkt.payload, pkt.payload_len, &back), CodecStatus::Ok);
  EXPECT_EQ(back.seconds_since_epoch, t.seconds_since_epoch);
  EXPECT_EQ(back.mechanisms_deployed_flags, t.mechanisms_deployed_flags);
}

TEST(Integration, CommandMechanismArrivesInReceivePacket) {
  uint8_t payload[kMaxMessageSize];
  size_t plen = 0;
  ASSERT_EQ(serialize(CommandMechanism{4, 1}, payload, sizeof(payload), &plen),
            CodecStatus::Ok);
  xbee::TransmitRequest req;
  uint8_t frame[xbee::kMaxTransmitFrameSize];
  size_t flen = 0;
  ASSERT_EQ(xbee::build_transmit_request(req, payload, plen, frame, sizeof(frame), &flen),
            xbee::Status::Ok);

  const auto rx = as_receive_packet(frame, flen);
  xbee::ReceivePacket pkt;
  ASSERT_EQ(xbee::parse_receive_packet(rx.data(), rx.size(), &pkt), xbee::Status::Ok);
  DecodedCommand cmd;
  ASSERT_EQ(deserialize_command(pkt.payload, pkt.payload_len, &cmd), CodecStatus::Ok);
  EXPECT_EQ(cmd.type, MessageType::CommandMechanism);
  EXPECT_EQ(cmd.mechanism.mechanism_id, 4);
  EXPECT_EQ(cmd.mechanism.value, 1);
}

TEST(Integration, CommandTelemetryArrivesInReceivePacket) {
  uint8_t payload[kMaxMessageSize];
  size_t plen = 0;
  ASSERT_EQ(serialize(CommandTelemetry{true}, payload, sizeof(payload), &plen),
            CodecStatus::Ok);
  xbee::TransmitRequest req;
  uint8_t frame[xbee::kMaxTransmitFrameSize];
  size_t flen = 0;
  ASSERT_EQ(xbee::build_transmit_request(req, payload, plen, frame, sizeof(frame), &flen),
            xbee::Status::Ok);

  const auto rx = as_receive_packet(frame, flen);
  xbee::ReceivePacket pkt;
  ASSERT_EQ(xbee::parse_receive_packet(rx.data(), rx.size(), &pkt), xbee::Status::Ok);
  DecodedCommand cmd;
  ASSERT_EQ(deserialize_command(pkt.payload, pkt.payload_len, &cmd), CodecStatus::Ok);
  EXPECT_EQ(cmd.type, MessageType::CommandTelemetry);
  EXPECT_TRUE(cmd.telemetry.is_on);
}

TEST(Integration, FramingLayerCarriesAnyBytesWithoutInterpretingThem) {
  const uint8_t junk[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x7E};
  xbee::TransmitRequest req;
  uint8_t frame[64];
  size_t flen = 0;
  ASSERT_EQ(xbee::build_transmit_request(req, junk, sizeof(junk), frame, sizeof(frame), &flen),
            xbee::Status::Ok);
  const auto rx = as_receive_packet(frame, flen);
  xbee::ReceivePacket pkt;
  ASSERT_EQ(xbee::parse_receive_packet(rx.data(), rx.size(), &pkt), xbee::Status::Ok);
  ASSERT_EQ(pkt.payload_len, sizeof(junk));
  EXPECT_EQ(0, std::memcmp(pkt.payload, junk, sizeof(junk)));
  DecodedCommand cmd;
  EXPECT_EQ(deserialize_command(pkt.payload, pkt.payload_len, &cmd), CodecStatus::UnknownType);
}

TEST(Integration, ValidFrameInvalidPayloadAreDistinctFailures) {
  const uint8_t bad_bool[] = {0x02, 0x09};
  xbee::TransmitRequest req;
  uint8_t frame[64];
  size_t flen = 0;
  ASSERT_EQ(xbee::build_transmit_request(req, bad_bool, sizeof(bad_bool), frame,
                                         sizeof(frame), &flen),
            xbee::Status::Ok);
  const auto rx = as_receive_packet(frame, flen);
  xbee::ReceivePacket pkt;
  ASSERT_EQ(xbee::parse_receive_packet(rx.data(), rx.size(), &pkt), xbee::Status::Ok);
  DecodedCommand cmd;
  EXPECT_EQ(deserialize_command(pkt.payload, pkt.payload_len, &cmd), CodecStatus::InvalidValue);
}

TEST(Integration, CorruptedFrameNeverReachesPayloadLayer) {
  const uint8_t good[] = {0x02, 0x01};
  xbee::TransmitRequest req;
  uint8_t frame[64];
  size_t flen = 0;
  ASSERT_EQ(xbee::build_transmit_request(req, good, sizeof(good), frame, sizeof(frame), &flen),
            xbee::Status::Ok);
  auto rx = as_receive_packet(frame, flen);
  rx[15] ^= 0x01;  // flip the "on" bit in transit
  xbee::ReceivePacket pkt;
  EXPECT_EQ(xbee::parse_receive_packet(rx.data(), rx.size(), &pkt), xbee::Status::BadChecksum);
}
