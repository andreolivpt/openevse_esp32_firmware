#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <initializer_list>
#include "voltage_throttle_logic.h"

static VoltageThrottleWindow window(std::initializer_list<double> volts)
{
  VoltageThrottleWindow w;
  for(double v : volts) {
    w.add(v);
  }
  return w;
}

// 212 / 218 V band, 1 A steps, 6..32 A charger
static const VoltageThrottleConfig cfg = { 212.0, 218.0, 1, 6, 32 };
static const uint32_t NONE = VOLTAGE_THROTTLE_NO_LIMIT;

TEST_CASE("window statistics") {
  auto w = window({ 210, 214, 216 });
  CHECK(w.count == 3);
  CHECK(w.mean() == doctest::Approx(213.333).epsilon(0.001));
  CHECK(w.min == doctest::Approx(210));
  w.reset();
  CHECK(w.count == 0);
}

TEST_CASE("no live voltage releases the limit") {
  VoltageThrottleWindow empty;
  CHECK(voltage_throttle_next_limit(NONE, 32, 10, empty, cfg) == NONE);
  CHECK(voltage_throttle_next_limit(8, 32, 8, empty, cfg) == NONE);
}

TEST_CASE("healthy voltage does nothing") {
  CHECK(voltage_throttle_next_limit(NONE, 32, 30, window({ 225, 226, 224 }), cfg) == NONE);
  // inside the band, not throttling yet
  CHECK(voltage_throttle_next_limit(NONE, 32, 30, window({ 214, 215 }), cfg) == NONE);
}

TEST_CASE("low voltage starts from the pilot when the car takes it") {
  CHECK(voltage_throttle_next_limit(NONE, 16, 15.8, window({ 210, 211, 209 }), cfg) == 15);
}

TEST_CASE("low voltage starts from the car's own draw when it is lower") {
  // pilot 32 A but the car limits itself to 10 A: 31 A would change nothing
  CHECK(voltage_throttle_next_limit(NONE, 32, 10.089, window({ 210 }), cfg) == 9);
  // the same once throttling with a limit still above the draw
  CHECK(voltage_throttle_next_limit(20, 32, 10.1, window({ 210 }), cfg) == 9);
}

TEST_CASE("a single dip does not move the limit, the mean does") {
  // one 205 V sample among healthy ones: mean 214.8, inside the band
  CHECK(voltage_throttle_next_limit(NONE, 16, 16, window({ 217, 216, 205, 218, 218 }), cfg) == NONE);
  CHECK(voltage_throttle_next_limit(12, 16, 12, window({ 217, 216, 205, 218, 218 }), cfg) == 12);
}

TEST_CASE("never below the minimum current") {
  CHECK(voltage_throttle_next_limit(7, 32, 7, window({ 200 }), cfg) == 6);
  CHECK(voltage_throttle_next_limit(6, 32, 6, window({ 200 }), cfg) == 6);
  // the car drawing almost nothing (ramping up) still floors at the minimum
  CHECK(voltage_throttle_next_limit(NONE, 32, 1.2, window({ 205 }), cfg) == 6);
}

TEST_CASE("recovery needs every sample above the high threshold") {
  CHECK(voltage_throttle_next_limit(9, 32, 9, window({ 219, 220, 221 }), cfg) == 10);
  // one sample at 217 V holds
  CHECK(voltage_throttle_next_limit(9, 32, 9, window({ 219, 217, 221 }), cfg) == 9);
}

TEST_CASE("a limit the car stays well below is dropped on recovery") {
  // car limited to 10 A by its own setting
  CHECK(voltage_throttle_next_limit(11, 32, 10.1, window({ 220 }), cfg) == 12);
  CHECK(voltage_throttle_next_limit(12, 32, 10.1, window({ 220 }), cfg) == NONE);
  // still binding: keep stepping
  CHECK(voltage_throttle_next_limit(12, 32, 11.9, window({ 220 }), cfg) == 13);
}

TEST_CASE("reaching the maximum releases the limit") {
  CHECK(voltage_throttle_next_limit(31, 32, 31, window({ 225 }), cfg) == NONE);
  VoltageThrottleConfig big_step = cfg;
  big_step.step = 4;
  CHECK(voltage_throttle_next_limit(30, 32, 30, window({ 225 }), big_step) == NONE);
}

TEST_CASE("larger steps") {
  VoltageThrottleConfig big_step = cfg;
  big_step.step = 2;
  CHECK(voltage_throttle_next_limit(NONE, 16, 16, window({ 205 }), big_step) == 14);
  CHECK(voltage_throttle_next_limit(7, 16, 7, window({ 205 }), big_step) == 6);
  CHECK(voltage_throttle_next_limit(14, 16, 14, window({ 225 }), big_step) == 16);
}
