#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <string.h>
#include <vector>
#include "modbus_meter_parser.h"

// Build a read holding registers response frame
static std::vector<uint8_t> response(uint16_t tid, uint8_t unit, const std::vector<uint16_t> &regs)
{
  uint16_t length = 3 + regs.size() * 2;
  std::vector<uint8_t> f = {
    (uint8_t)(tid >> 8), (uint8_t)tid, 0, 0,
    (uint8_t)(length >> 8), (uint8_t)length, unit, 0x03, (uint8_t)(regs.size() * 2)
  };
  for(uint16_t r : regs) {
    f.push_back(r >> 8);
    f.push_back(r & 0xff);
  }
  return f;
}

static void float_to_regs(float v, uint16_t *regs)
{
  uint32_t raw;
  memcpy(&raw, &v, sizeof(raw));
  regs[0] = raw >> 16;
  regs[1] = raw & 0xffff;
}

TEST_CASE("build read request") {
  uint8_t buf[MODBUS_TCP_READ_REQUEST_LEN];
  CHECK(modbus_tcp_build_read(buf, sizeof(buf), 0x1234, 1, 0x2000, 6) == 12);
  const uint8_t expected[] = { 0x12, 0x34, 0, 0, 0, 6, 1, 0x03, 0x20, 0x00, 0x00, 0x06 };
  CHECK(memcmp(buf, expected, sizeof(expected)) == 0);
}

TEST_CASE("build read request rejects bad arguments") {
  uint8_t buf[MODBUS_TCP_READ_REQUEST_LEN];
  CHECK(modbus_tcp_build_read(buf, 11, 1, 1, 0x2000, 6) == 0);
  CHECK(modbus_tcp_build_read(buf, sizeof(buf), 1, 1, 0x2000, 0) == 0);
  CHECK(modbus_tcp_build_read(buf, sizeof(buf), 1, 1, 0x2000, 126) == 0);
}

TEST_CASE("decode DDSU666 floats, high word first") {
  // 230.5 V as stored by the meter: 0x4366 0x8000
  uint16_t volts[2] = { 0x4366, 0x8000 };
  CHECK(modbus_regs_to_float(volts) == doctest::Approx(230.5f));

  uint16_t regs[2];
  float_to_regs(7.36f, regs);
  CHECK(modbus_regs_to_float(regs) == doctest::Approx(7.36f));
}

TEST_CASE("parse U, I, P block") {
  std::vector<uint16_t> regs(6);
  float_to_regs(231.2f, &regs[0]);
  float_to_regs(31.8f, &regs[2]);
  float_to_regs(7.35f, &regs[4]); // kW
  auto frame = response(7, 1, regs);

  uint16_t out[6];
  size_t consumed = 0;
  uint8_t exc = 0;
  CHECK(modbus_tcp_parse_read(frame.data(), frame.size(), 7, 6, out, consumed, exc) == ModbusParseResult::Ok);
  CHECK(consumed == frame.size());
  CHECK(modbus_regs_to_float(&out[0]) == doctest::Approx(231.2f));
  CHECK(modbus_regs_to_float(&out[2]) == doctest::Approx(31.8f));
  CHECK(modbus_regs_to_float(&out[4]) == doctest::Approx(7.35f));
}

TEST_CASE("partial frames need more data") {
  auto frame = response(1, 1, { 1, 2 });
  uint16_t out[2];
  size_t consumed = 0;
  uint8_t exc = 0;
  for(size_t n = 0; n < frame.size(); n++) {
    CHECK(modbus_tcp_parse_read(frame.data(), n, 1, 2, out, consumed, exc) == ModbusParseResult::NeedMore);
    CHECK(consumed == 0);
  }
}

TEST_CASE("stale transaction is skipped, then the right one parses") {
  auto stale = response(1, 1, { 0xaaaa, 0xbbbb });
  auto good = response(2, 1, { 0x0102, 0x0304 });
  std::vector<uint8_t> stream(stale);
  stream.insert(stream.end(), good.begin(), good.end());

  uint16_t out[2];
  size_t consumed = 0;
  uint8_t exc = 0;
  CHECK(modbus_tcp_parse_read(stream.data(), stream.size(), 2, 2, out, consumed, exc) == ModbusParseResult::WrongTransaction);
  CHECK(consumed == stale.size());
  CHECK(modbus_tcp_parse_read(stream.data() + consumed, stream.size() - consumed, 2, 2, out, consumed, exc) == ModbusParseResult::Ok);
  CHECK(out[0] == 0x0102);
  CHECK(out[1] == 0x0304);
}

TEST_CASE("exception response") {
  // Illegal data address
  std::vector<uint8_t> frame = { 0, 5, 0, 0, 0, 3, 1, 0x83, 0x02 };
  uint16_t out[2];
  size_t consumed = 0;
  uint8_t exc = 0;
  CHECK(modbus_tcp_parse_read(frame.data(), frame.size(), 5, 2, out, consumed, exc) == ModbusParseResult::Exception);
  CHECK(exc == 2);
  CHECK(consumed == frame.size());
}

TEST_CASE("bad frames") {
  uint16_t out[2];
  size_t consumed = 0;
  uint8_t exc = 0;

  // Not Modbus (protocol id != 0), e.g. an RTU-over-TCP gateway
  std::vector<uint8_t> rtu = { 0x01, 0x03, 0x04, 0x43, 0x66, 0x80, 0x00, 0xAB, 0xCD };
  CHECK(modbus_tcp_parse_read(rtu.data(), rtu.size(), 0x0103, 2, out, consumed, exc) == ModbusParseResult::BadFrame);

  // Wrong register count in the answer
  auto frame = response(3, 1, { 1, 2, 3 });
  CHECK(modbus_tcp_parse_read(frame.data(), frame.size(), 3, 2, out, consumed, exc) == ModbusParseResult::BadFrame);

  // Wrong function code
  auto fc = response(4, 1, { 1, 2 });
  fc[7] = 0x04;
  CHECK(modbus_tcp_parse_read(fc.data(), fc.size(), 4, 2, out, consumed, exc) == ModbusParseResult::BadFrame);
}
