#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <math.h>
#include <string.h>
#include <vector>
#include "modbus_meter_parser.h"
#include "current_shaper_limit.h"

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

TEST_CASE("energy delta from the import register") {
  double wh = -1;
  // 10 Wh in 5 s (7.2 kW)
  CHECK(modbus_meter_energy_delta(1234.56, 1234.57, 5000, wh));
  CHECK(wh == doctest::Approx(10.0));

  // No change
  CHECK(modbus_meter_energy_delta(10.0, 10.0, 2000, wh));
  CHECK(wh == doctest::Approx(0.0));

  // A few polls of accumulated energy still fits the 60 s minimum window
  CHECK(modbus_meter_energy_delta(100.0, 100.12, 1000, wh));
  CHECK(wh == doctest::Approx(120.0));
}

TEST_CASE("energy delta rejects untrustworthy steps") {
  double wh = -1;
  // Meter reset / replaced
  CHECK_FALSE(modbus_meter_energy_delta(1234.0, 3.0, 5000, wh));
  // Absurd jump (wrong register, swapped meter)
  CHECK_FALSE(modbus_meter_energy_delta(100.0, 101.0, 5000, wh));
  // No previous value / gap
  CHECK_FALSE(modbus_meter_energy_delta(NAN, 10.0, 5000, wh));
  CHECK_FALSE(modbus_meter_energy_delta(10.0, NAN, 5000, wh));
  CHECK(wh == -1); // untouched
  // Long gap allows proportionally more: 1 h at 7 kW
  CHECK(modbus_meter_energy_delta(100.0, 107.0, 3600000, wh));
}

TEST_CASE("value formats") {
  ModbusValueFormat f = ModbusValueFormat::Int16;
  CHECK(modbus_value_format_parse("int32", f));
  CHECK(f == ModbusValueFormat::Int32);
  CHECK(modbus_value_format_regs(f) == 2);
  CHECK_FALSE(modbus_value_format_parse("int64", f));
  CHECK(f == ModbusValueFormat::Int32); // untouched
  CHECK_FALSE(modbus_value_format_parse(nullptr, f));

  // AlphaESS grid power, int32 high word first: -1234 W and 5432 W
  uint16_t neg[2] = { 0xFFFF, 0xFB2E };
  CHECK(modbus_decode_value(neg, ModbusValueFormat::Int32) == doctest::Approx(-1234));
  uint16_t pos[2] = { 0x0000, 0x1538 };
  CHECK(modbus_decode_value(pos, ModbusValueFormat::Int32) == doctest::Approx(5432));
  CHECK(modbus_decode_value(neg, ModbusValueFormat::UInt32) == doctest::Approx(4294966062.0));

  uint16_t s16[1] = { 0xFFF6 };
  CHECK(modbus_decode_value(s16, ModbusValueFormat::Int16) == doctest::Approx(-10));
  CHECK(modbus_decode_value(s16, ModbusValueFormat::UInt16) == doctest::Approx(65526));

  uint16_t fl[2] = { 0x4366, 0x8000 };
  CHECK(modbus_decode_value(fl, ModbusValueFormat::Float32) == doctest::Approx(230.5));
}

// AlphaESS grid block as read on a real system (0x0014 .. 0x0032):
// 235 V, 1.8 A, -7 W, -291 var, 423 VA
static std::vector<uint16_t> alphaess_block()
{
  std::vector<uint16_t> r(ALPHAESS_GRID_BLOCK_COUNT, 0);
  auto at = [&](uint16_t addr) -> uint16_t & { return r[addr - ALPHAESS_GRID_BLOCK_START]; };
  at(0x0014) = 235;
  at(0x0017) = 18;
  at(0x001A) = 4997;
  at(0x001B) = 0xFFFF; at(0x001C) = 0xFFF9; // -7 W phase A
  at(0x0021) = 0xFFFF; at(0x0022) = 0xFFF9; // -7 W total
  at(0x0029) = 0xFFFF; at(0x002A) = 0xFEDD; // -291 var
  at(0x002B) = 0;      at(0x002C) = 423;
  at(0x0031) = 0;      at(0x0032) = 423;    // 423 VA total
  return r;
}

TEST_CASE("AlphaESS grid block decodes the real reading") {
  std::vector<uint16_t> r = alphaess_block();
  ModbusGridSample s;
  REQUIRE(modbus_alphaess_grid_decode(r.data(), r.size(), s));
  CHECK(s.voltage == doctest::Approx(235));
  CHECK(s.power == doctest::Approx(-7));
  CHECK(s.apparent == doctest::Approx(423));
  // Exporting 7 W: only the active part counts as headroom, not the 1.8 A
  // of reactive current
  CHECK(s.current == doctest::Approx(-7.0 / 235.0));
}

TEST_CASE("AlphaESS grid block while importing") {
  std::vector<uint16_t> r = alphaess_block();
  r[0x0017 - ALPHAESS_GRID_BLOCK_START] = 254;  // 25.4 A
  r[0x0021 - ALPHAESS_GRID_BLOCK_START] = 0;
  r[0x0022 - ALPHAESS_GRID_BLOCK_START] = 5800; // 5800 W
  ModbusGridSample s;
  REQUIRE(modbus_alphaess_grid_decode(r.data(), r.size(), s));
  CHECK(s.current == doctest::Approx(25.4));
  CHECK(s.power == doctest::Approx(5800));
}

TEST_CASE("AlphaESS grid block rejects bad readings") {
  ModbusGridSample s;
  std::vector<uint16_t> r = alphaess_block();
  CHECK_FALSE(modbus_alphaess_grid_decode(r.data(), r.size() - 1, s));
  CHECK_FALSE(modbus_alphaess_grid_decode(nullptr, ALPHAESS_GRID_BLOCK_COUNT, s));

  r[0x0014 - ALPHAESS_GRID_BLOCK_START] = 0; // no voltage
  CHECK_FALSE(modbus_alphaess_grid_decode(r.data(), r.size(), s));

  r = alphaess_block();
  r[0x0017 - ALPHAESS_GRID_BLOCK_START] = 30000; // 3000 A
  CHECK_FALSE(modbus_alphaess_grid_decode(r.data(), r.size(), s));
}

TEST_CASE("signed grid current") {
  CHECK(modbus_grid_signed_current(10, 2300, 230) == doctest::Approx(10));
  CHECK(modbus_grid_signed_current(-10, 2300, 230) == doctest::Approx(10));
  CHECK(modbus_grid_signed_current(1.8, 0, 235) == doctest::Approx(1.8));
  // exporting 2300 W at 230 V: -10 A
  CHECK(modbus_grid_signed_current(10.5, -2300, 230) == doctest::Approx(-10));
  // never more headroom than the measured current
  CHECK(modbus_grid_signed_current(5, -2300, 230) == doctest::Approx(-5));
  CHECK(modbus_grid_signed_current(5, -2300, 0) == doctest::Approx(5));
}

TEST_CASE("shaper grid current limit") {
  // amps only
  CHECK(shaper_grid_current_limit(29, 0, 0, 235) == doctest::Approx(29));
  // kVA only, follows the voltage
  CHECK(shaper_grid_current_limit(0, 6900, 0, 230) == doctest::Approx(30));
  CHECK(shaper_grid_current_limit(0, 6900, 0, 235) == doctest::Approx(6900.0 / 235));
  // both: the lower one
  CHECK(shaper_grid_current_limit(29, 6900, 0, 215) == doctest::Approx(29));
  CHECK(shaper_grid_current_limit(29, 6900, 0, 240) == doctest::Approx(28.75));
  // neither: the shaper's max power as VA
  CHECK(shaper_grid_current_limit(0, 0, 6000, 240) == doctest::Approx(25));
  // nothing usable
  CHECK(shaper_grid_current_limit(0, 0, 0, 230) == doctest::Approx(0));
  CHECK(shaper_grid_current_limit(0, 6900, 0, 0) == doctest::Approx(0));
}

TEST_CASE("vehicle current from the grid current") {
  // house 10 A + car 16 A = 26 A on the grid, limit 29 A: car may take 19 A
  CHECK(shaper_ev_current_from_grid(29, 26, 16) == doctest::Approx(19));
  // stove on: grid 36 A with the car at 16 A, car must drop to 9 A
  CHECK(shaper_ev_current_from_grid(29, 36, 16) == doctest::Approx(9));
}
