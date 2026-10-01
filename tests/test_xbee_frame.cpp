#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "cansat_comms/xbee_frame.hpp"

using namespace cansat::xbee;

namespace {

// examples from the digi xbee api docs
const uint8_t kDigiTxUnicast[] = {0x7E, 0x00, 0x14, 0x10, 0x52, 0x00, 0x13, 0xA2,
                                  0x00, 0x12, 0x34, 0x56, 0x78, 0xFF, 0xFE, 0x00,
                                  0x00, 0x54, 0x78, 0x44, 0x61, 0x74, 0x61, 0x91};

const uint8_t kDigiTxBroadcast[] = {0x7E, 0x00, 0x17, 0x10, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFE, 0x01,
                                    0x00, 0x42, 0x72, 0x6F, 0x61, 0x64, 0x63, 0x61,
                                    0x73, 0x74, 0x60};

const uint8_t kDigiRx[] = {0x7E, 0x00, 0x12, 0x90, 0x00, 0x13, 0xA2, 0x00,
                           0x41, 0xAE, 0xB5, 0x4E, 0xFF, 0xFE, 0xC1, 0x54,
                           0x78, 0x44, 0x61, 0x74, 0x61, 0xC4};

const uint8_t kTxData[] = {0x54, 0x78, 0x44, 0x61, 0x74, 0x61};  // "TxData"

std::vector<uint8_t> rx_frame(const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> f = {0x7E, 0, 0, 0x90, 0, 0x13, 0xA2, 0, 0x41, 0xAE,
                            0xB5, 0x4E, 0xFF, 0xFE, 0xC1};
  f.insert(f.end(), payload.begin(), payload.end());
  const size_t len = f.size() - 3;
  f[1] = static_cast<uint8_t>(len >> 8);
  f[2] = static_cast<uint8_t>(len);
  f.push_back(compute_checksum(&f[3], f.size() - 3));
  return f;
}

}  // namespace


TEST(Checksum, DigiTxExample) {
  EXPECT_EQ(compute_checksum(&kDigiTxUnicast[3], 20), 0x91);
}

TEST(Checksum, DigiRxExample) {
  EXPECT_EQ(compute_checksum(&kDigiRx[3], 18), 0xC4);
}

TEST(Checksum, VerifySumPlusChecksumIsFF) {
  const uint8_t data[] = {0x10, 0x01, 0x02, 0x03};
  const uint8_t cs = compute_checksum(data, sizeof(data));
  EXPECT_TRUE(verify_checksum(data, sizeof(data), cs));
  EXPECT_FALSE(verify_checksum(data, sizeof(data), static_cast<uint8_t>(cs + 1)));
}

TEST(Checksum, EmptyInputIsFF) {
  EXPECT_EQ(compute_checksum(nullptr, 0), 0xFF);
  const uint8_t d = 0;
  EXPECT_EQ(compute_checksum(&d, 0), 0xFF);
}

TEST(Checksum, WrapsModulo256) {
  const uint8_t data[] = {0xFF, 0xFF, 0xFF};  // sum is 0x2fd, low byte 0xfd
  EXPECT_EQ(compute_checksum(data, 3), static_cast<uint8_t>(0xFF - 0xFD));
}


TEST(TransmitRequest, MatchesDigiUnicastExample) {
  TransmitRequest req;
  req.destination = 0x0013A20012345678ULL;
  req.frame_id = 0x52;
  uint8_t out[64];
  size_t n = 0;
  ASSERT_EQ(build_transmit_request(req, kTxData, sizeof(kTxData), out, sizeof(out), &n),
            Status::Ok);
  ASSERT_EQ(n, sizeof(kDigiTxUnicast));
  EXPECT_EQ(0, std::memcmp(out, kDigiTxUnicast, n));
}

TEST(TransmitRequest, MatchesDigiBroadcastExample) {
  TransmitRequest req;
  req.destination = kBroadcastAddress;
  req.frame_id = 0;
  req.broadcast_radius = 0x01;
  req.options = 0x00;
  const uint8_t payload[] = {0x42, 0x72, 0x6F, 0x61, 0x64, 0x63, 0x61, 0x73, 0x74};
  uint8_t out[64];
  size_t n = 0;
  ASSERT_EQ(build_transmit_request(req, payload, sizeof(payload), out, sizeof(out), &n),
            Status::Ok);
  ASSERT_EQ(n, sizeof(kDigiTxBroadcast));
  EXPECT_EQ(0, std::memcmp(out, kDigiTxBroadcast, n));
}

TEST(TransmitRequest, LengthFieldIs14PlusPayload) {
  TransmitRequest req;
  for (size_t plen : {size_t{0}, size_t{1}, size_t{7}, kMaxPayloadSize}) {
    std::vector<uint8_t> payload(plen, 0xAB);
    std::vector<uint8_t> out(kMaxTransmitFrameSize);
    size_t n = 0;
    ASSERT_EQ(build_transmit_request(req, payload.data(), plen, out.data(), out.size(), &n),
              Status::Ok);
    EXPECT_EQ(n, 4 + 14 + plen);
    const size_t len_field = (static_cast<size_t>(out[1]) << 8) | out[2];
    EXPECT_EQ(len_field, 14 + plen);
    EXPECT_EQ(out[0], 0x7E);
    EXPECT_EQ(out[3], 0x10);
    EXPECT_TRUE(verify_checksum(&out[3], n - 4, out[n - 1]));
  }
}

TEST(TransmitRequest, ZeroLengthPayloadAllowedWithNullPointer) {
  TransmitRequest req;
  uint8_t out[32];
  size_t n = 0;
  EXPECT_EQ(build_transmit_request(req, nullptr, 0, out, sizeof(out), &n), Status::Ok);
  EXPECT_EQ(n, 18u);
}

TEST(TransmitRequest, PayloadTooLarge) {
  TransmitRequest req;
  std::vector<uint8_t> payload(kMaxPayloadSize + 1, 0);
  std::vector<uint8_t> out(kMaxTransmitFrameSize + 16, 0xEE);
  size_t n = 123;
  EXPECT_EQ(build_transmit_request(req, payload.data(), payload.size(), out.data(),
                                   out.size(), &n),
            Status::PayloadTooLarge);
  EXPECT_EQ(out[0], 0xEE);
}

TEST(TransmitRequest, BufferTooSmallWritesNothing) {
  TransmitRequest req;
  uint8_t out[10];
  std::memset(out, 0xEE, sizeof(out));
  EXPECT_EQ(build_transmit_request(req, kTxData, sizeof(kTxData), out, sizeof(out), nullptr),
            Status::BufferTooSmall);
  for (uint8_t b : out) EXPECT_EQ(b, 0xEE);
}

TEST(TransmitRequest, ExactCapacityFits) {
  TransmitRequest req;
  uint8_t out[sizeof(kDigiTxUnicast)];
  size_t n = 0;
  EXPECT_EQ(build_transmit_request(req, kTxData, sizeof(kTxData), out, sizeof(out), &n),
            Status::Ok);
  EXPECT_EQ(n, sizeof(out));
}

TEST(TransmitRequest, OneByteShortOfCapacityFails) {
  TransmitRequest req;
  uint8_t out[sizeof(kDigiTxUnicast) - 1];
  EXPECT_EQ(build_transmit_request(req, kTxData, sizeof(kTxData), out, sizeof(out), nullptr),
            Status::BufferTooSmall);
}

TEST(TransmitRequest, NullArguments) {
  TransmitRequest req;
  uint8_t out[64];
  EXPECT_EQ(build_transmit_request(req, kTxData, sizeof(kTxData), nullptr, 64, nullptr),
            Status::NullArgument);
  EXPECT_EQ(build_transmit_request(req, nullptr, 5, out, sizeof(out), nullptr),
            Status::NullArgument);
}

TEST(TransmitRequest, OutLenOptional) {
  TransmitRequest req;
  uint8_t out[64];
  EXPECT_EQ(build_transmit_request(req, kTxData, sizeof(kTxData), out, sizeof(out), nullptr),
            Status::Ok);
}

TEST(TransmitRequest, PayloadWithFramingBytesIsNotEscaped) {
  // ap=1, so bytes like 0x7e go through unescaped
  TransmitRequest req;
  const uint8_t payload[] = {0x7E, 0x7D, 0x11, 0x13};
  uint8_t out[64];
  size_t n = 0;
  ASSERT_EQ(build_transmit_request(req, payload, sizeof(payload), out, sizeof(out), &n),
            Status::Ok);
  EXPECT_EQ(0, std::memcmp(&out[17], payload, sizeof(payload)));
  EXPECT_TRUE(verify_checksum(&out[3], n - 4, out[n - 1]));
}


TEST(ReceivePacket, ParsesDigiExample) {
  ReceivePacket pkt;
  ASSERT_EQ(parse_receive_packet(kDigiRx, sizeof(kDigiRx), &pkt), Status::Ok);
  EXPECT_EQ(pkt.source_address, 0x0013A20041AEB54EULL);
  EXPECT_EQ(pkt.reserved, 0xFFFE);
  EXPECT_EQ(pkt.receive_options, 0xC1);
  ASSERT_EQ(pkt.payload_len, sizeof(kTxData));
  EXPECT_EQ(0, std::memcmp(pkt.payload, kTxData, sizeof(kTxData)));
  EXPECT_EQ(pkt.frame_len, sizeof(kDigiRx));
}

TEST(ReceivePacket, PayloadIsZeroCopyViewIntoInput) {
  ReceivePacket pkt;
  ASSERT_EQ(parse_receive_packet(kDigiRx, sizeof(kDigiRx), &pkt), Status::Ok);
  EXPECT_EQ(pkt.payload, &kDigiRx[15]);
}

TEST(ReceivePacket, EmptyPayload) {
  const auto f = rx_frame({});
  ReceivePacket pkt;
  ASSERT_EQ(parse_receive_packet(f.data(), f.size(), &pkt), Status::Ok);
  EXPECT_EQ(pkt.payload_len, 0u);
  EXPECT_EQ(pkt.payload, nullptr);
}

TEST(ReceivePacket, MaxPayload) {
  const std::vector<uint8_t> payload(kMaxPayloadSize, 0x5A);
  const auto f = rx_frame(payload);
  ReceivePacket pkt;
  ASSERT_EQ(parse_receive_packet(f.data(), f.size(), &pkt), Status::Ok);
  EXPECT_EQ(pkt.payload_len, kMaxPayloadSize);
}

TEST(ReceivePacket, PayloadOverMaxRejectedAsBadLength) {
  const std::vector<uint8_t> payload(kMaxPayloadSize + 1, 0x5A);
  const auto f = rx_frame(payload);
  ReceivePacket pkt;
  EXPECT_EQ(parse_receive_packet(f.data(), f.size(), &pkt), Status::BadLength);
}

TEST(ReceivePacket, TrailingBytesAfterFrameAreIgnoredAndFrameLenReported) {
  std::vector<uint8_t> buf(kDigiRx, kDigiRx + sizeof(kDigiRx));
  buf.push_back(0x7E);  // start of the next frame
  buf.push_back(0x00);
  ReceivePacket pkt;
  ASSERT_EQ(parse_receive_packet(buf.data(), buf.size(), &pkt), Status::Ok);
  EXPECT_EQ(pkt.frame_len, sizeof(kDigiRx));
}

TEST(ReceivePacket, BadStartDelimiter) {
  uint8_t f[sizeof(kDigiRx)];
  std::memcpy(f, kDigiRx, sizeof(f));
  f[0] = 0x7F;
  ReceivePacket pkt;
  EXPECT_EQ(parse_receive_packet(f, sizeof(f), &pkt), Status::BadStartDelimiter);
}

TEST(ReceivePacket, EmptyBufferIsIncomplete) {
  ReceivePacket pkt;
  EXPECT_EQ(parse_receive_packet(kDigiRx, 0, &pkt), Status::Incomplete);
}

TEST(ReceivePacket, EveryTruncationIsIncompleteNeverACrash) {
  for (size_t n = 0; n < sizeof(kDigiRx); ++n) {
    ReceivePacket pkt;
    EXPECT_EQ(parse_receive_packet(kDigiRx, n, &pkt), Status::Incomplete) << "n=" << n;
  }
}

TEST(ReceivePacket, LengthTooSmallForHeader) {
  uint8_t f[sizeof(kDigiRx)];
  std::memcpy(f, kDigiRx, sizeof(f));
  f[1] = 0x00;
  f[2] = 0x05;  // < 12: checksum is then read from the wrong byte
  ReceivePacket pkt;
  EXPECT_NE(parse_receive_packet(f, sizeof(f), &pkt), Status::Ok);
}

TEST(ReceivePacket, LengthZero) {
  const uint8_t f[] = {0x7E, 0x00, 0x00, 0xFF};
  ReceivePacket pkt;
  EXPECT_EQ(parse_receive_packet(f, sizeof(f), &pkt), Status::BadLength);
}

TEST(ReceivePacket, LengthHugeIsRejectedWithoutReadingPastBuffer) {
  const uint8_t f[] = {0x7E, 0xFF, 0xFF, 0x90, 0x00};
  ReceivePacket pkt;
  EXPECT_EQ(parse_receive_packet(f, sizeof(f), &pkt), Status::BadLength);
}

TEST(ReceivePacket, LengthLargerThanDataIsIncomplete) {
  uint8_t f[sizeof(kDigiRx)];
  std::memcpy(f, kDigiRx, sizeof(f));
  f[2] = 0x13;  // claims one more byte than present
  ReceivePacket pkt;
  EXPECT_EQ(parse_receive_packet(f, sizeof(f), &pkt), Status::Incomplete);
}

TEST(ReceivePacket, LengthSmallerThanDataFailsChecksum) {
  uint8_t f[sizeof(kDigiRx)];
  std::memcpy(f, kDigiRx, sizeof(f));
  f[2] = 0x11;  // one byte short: checksum is read from the wrong position
  ReceivePacket pkt;
  EXPECT_EQ(parse_receive_packet(f, sizeof(f), &pkt), Status::BadChecksum);
}

TEST(ReceivePacket, BadChecksum) {
  uint8_t f[sizeof(kDigiRx)];
  std::memcpy(f, kDigiRx, sizeof(f));
  f[sizeof(f) - 1] ^= 0x01;
  ReceivePacket pkt;
  EXPECT_EQ(parse_receive_packet(f, sizeof(f), &pkt), Status::BadChecksum);
}

TEST(ReceivePacket, CorruptedPayloadByteFailsChecksum) {
  uint8_t f[sizeof(kDigiRx)];
  std::memcpy(f, kDigiRx, sizeof(f));
  f[17] ^= 0x40;
  ReceivePacket pkt;
  EXPECT_EQ(parse_receive_packet(f, sizeof(f), &pkt), Status::BadChecksum);
}

TEST(ReceivePacket, SingleBitFlipAnywhereIsDetected) {
  for (size_t i = 0; i < sizeof(kDigiRx); ++i) {
    for (int bit = 0; bit < 8; ++bit) {
      uint8_t f[sizeof(kDigiRx)];
      std::memcpy(f, kDigiRx, sizeof(f));
      f[i] ^= static_cast<uint8_t>(1u << bit);
      ReceivePacket pkt;
      EXPECT_NE(parse_receive_packet(f, sizeof(f), &pkt), Status::Ok)
          << "byte " << i << " bit " << bit;
    }
  }
}

TEST(ReceivePacket, TransmitStatusFrameReportedAsUnexpectedTypeWithLength) {
  // 0x8b transmit status, sent by the radio when frame_id != 0
  const uint8_t f[] = {0x7E, 0x00, 0x07, 0x8B, 0x01, 0xFF, 0xFE, 0x00, 0x00, 0x00, 0x76};
  ASSERT_EQ(compute_checksum(&f[3], 7), 0x76);
  ReceivePacket pkt;
  pkt.payload_len = 99;
  EXPECT_EQ(parse_receive_packet(f, sizeof(f), &pkt), Status::UnexpectedFrameType);
  EXPECT_EQ(pkt.frame_len, sizeof(f));
  EXPECT_EQ(pkt.payload_len, 99u);
}

TEST(ReceivePacket, ModemStatusFrameIsSkippable) {
  const uint8_t f[] = {0x7E, 0x00, 0x02, 0x8A, 0x06, 0x6F};  // modem status: coordinator started
  ReceivePacket pkt;
  EXPECT_EQ(parse_receive_packet(f, sizeof(f), &pkt), Status::UnexpectedFrameType);
  EXPECT_EQ(pkt.frame_len, sizeof(f));
}

TEST(ReceivePacket, Type90TooShortForHeaderIsBadLength) {
  std::vector<uint8_t> f = {0x7E, 0x00, 0x05, 0x90, 1, 2, 3, 4};
  f.push_back(compute_checksum(&f[3], 5));
  ReceivePacket pkt;
  EXPECT_EQ(parse_receive_packet(f.data(), f.size(), &pkt), Status::BadLength);
}

TEST(ReceivePacket, WrongFrameTypeLongEnough) {
  std::vector<uint8_t> f = rx_frame({1, 2, 3});
  f[3] = 0x91;  // explicit rx indicator
  f.back() = compute_checksum(&f[3], f.size() - 4);
  ReceivePacket pkt;
  EXPECT_EQ(parse_receive_packet(f.data(), f.size(), &pkt), Status::UnexpectedFrameType);
}

TEST(ReceivePacket, OutputUntouchedOnFailure) {
  ReceivePacket pkt;
  pkt.source_address = 0x1234;
  pkt.payload_len = 77;
  EXPECT_NE(parse_receive_packet(kDigiRx, 5, &pkt), Status::Ok);
  EXPECT_EQ(pkt.source_address, 0x1234u);
  EXPECT_EQ(pkt.payload_len, 77u);
}

TEST(ReceivePacket, NullArguments) {
  ReceivePacket pkt;
  EXPECT_EQ(parse_receive_packet(nullptr, 10, &pkt), Status::NullArgument);
  EXPECT_EQ(parse_receive_packet(kDigiRx, sizeof(kDigiRx), nullptr), Status::NullArgument);
}

TEST(ReceivePacket, GarbageNeverCrashes) {
  uint32_t state = 0x12345678;
  for (int iter = 0; iter < 2000; ++iter) {
    uint8_t buf[64];
    for (auto& b : buf) {
      state = state * 1664525u + 1013904223u;
      b = static_cast<uint8_t>(state >> 24);
    }
    if (iter & 1) buf[0] = 0x7E;
    ReceivePacket pkt;
    const size_t len = static_cast<size_t>(iter) % (sizeof(buf) + 1);
    (void)parse_receive_packet(buf, len, &pkt);
  }
}

TEST(StatusStrings, AllNamed) {
  EXPECT_STREQ(to_string(Status::Ok), "Ok");
  EXPECT_STREQ(to_string(Status::BadChecksum), "BadChecksum");
  EXPECT_STREQ(to_string(Status::Incomplete), "Incomplete");
}
