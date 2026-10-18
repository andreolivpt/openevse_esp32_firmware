#ifndef _VOLTAGE_THROTTLE_LOGIC_H
#define _VOLTAGE_THROTTLE_LOGIC_H

// Decision logic for the low voltage throttle, kept free of Arduino / EVSE
// dependencies so it can be unit tested on the host
// (see test/test_voltage_throttle).

#include <stdint.h>

#define VOLTAGE_THROTTLE_NO_LIMIT UINT32_MAX

// Voltage samples collected over one decision interval
struct VoltageThrottleWindow
{
  double sum;
  double min;
  uint32_t count;

  VoltageThrottleWindow() { reset(); }

  void reset() {
    sum = 0;
    min = 0;
    count = 0;
  }

  void add(double volts) {
    if(0 == count || volts < min) {
      min = volts;
    }
    sum += volts;
    count++;
  }

  double mean() const {
    return count > 0 ? sum / count : 0;
  }
};

struct VoltageThrottleConfig
{
  double low;            // V, below this (window mean) the limit goes down
  double high;           // V, above this (whole window) the limit goes up
  uint32_t step;         // A per decision
  uint32_t min_current;  // A, never limit below this (J1772 minimum)
  uint32_t max_current;  // A, reaching this removes the limit
};

// Work out the next current limit at the end of a decision interval.
//
//  limit    current limit, VOLTAGE_THROTTLE_NO_LIMIT when not throttling
//  pilot    current offered to the vehicle right now (A)
//  amps     current the vehicle actually draws (A); when the car limits
//           itself below the pilot, lowering the pilot from above that
//           draw would do nothing, so the limit starts from the draw
//
// Returns the new limit, or VOLTAGE_THROTTLE_NO_LIMIT to stop throttling:
//  - no samples (no live voltage): stop, back to the default behaviour
//  - mean below `low`: one step down from what is used now, not below
//    min_current
//  - every sample above `high`: one step up; reaching max_current stops,
//    and so does a limit the vehicle stays well below (no longer binding)
//  - otherwise (inside the hysteresis band): keep the limit
uint32_t voltage_throttle_next_limit(uint32_t limit, uint32_t pilot, double amps,
                                     const VoltageThrottleWindow &window,
                                     const VoltageThrottleConfig &config);

#endif // _VOLTAGE_THROTTLE_LOGIC_H
