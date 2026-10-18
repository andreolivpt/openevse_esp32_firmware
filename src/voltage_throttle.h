#ifndef _OPENEVSE_VOLTAGE_THROTTLE_H
#define _OPENEVSE_VOLTAGE_THROTTLE_H

// Low voltage throttle
//
// While charging with a live voltage source (Modbus meter, MQTT or Shelly
// LNM), the voltage is sampled every VOLTAGE_THROTTLE_SAMPLE_TIME and every
// `voltage_throttle_interval` seconds:
//  - window mean below `voltage_throttle_low`: the charge current limit
//    goes down by `voltage_throttle_step` A (not below the charger minimum)
//  - every sample above `voltage_throttle_high`: the limit goes up by one
//    step; reaching the configured maximum removes it
//  - in between: the limit is kept (hysteresis)
// Without a live voltage, or when not charging, the limit is removed.
//
// The limit is applied by EvseManager after the claims are resolved, as the
// lowest of all limits, so it never raises what the shaper, divert, a
// schedule, ... asked for.

#ifndef VOLTAGE_THROTTLE_SAMPLE_TIME
#define VOLTAGE_THROTTLE_SAMPLE_TIME 5000 // ms
#endif

#define VOLTAGE_THROTTLE_INTERVAL_MIN 10  // s
#define VOLTAGE_THROTTLE_INTERVAL_MAX 600 // s
#define VOLTAGE_THROTTLE_STEP_MAX     10  // A

#include <Arduino.h>
#include <MicroTasks.h>

#include "evse_man.h"
#include "voltage_throttle_logic.h"

class VoltageThrottleTask : public MicroTasks::Task
{
  private:
    EvseManager *_evse;
    VoltageThrottleWindow _window;
    uint32_t _windowStart;
    uint32_t _limit;
    double _lastMean;

    void setLimit(uint32_t limit);
    void decide();
    VoltageThrottleConfig getConfig();
    uint32_t getIntervalMs();

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    VoltageThrottleTask();

    void begin(EvseManager &evse);
    void notifyConfigChanged();

    bool isThrottling() {
      return VOLTAGE_THROTTLE_NO_LIMIT != _limit;
    }

    // Current limit in A, 0 when not throttling
    uint32_t getLimit() {
      return isThrottling() ? _limit : 0;
    }

    // Mean voltage of the last complete window, NAN when none
    double getLastMean() {
      return _lastMean;
    }
};

extern VoltageThrottleTask voltageThrottle;

#endif // _OPENEVSE_VOLTAGE_THROTTLE_H
