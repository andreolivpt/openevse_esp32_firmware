// Low voltage throttle, see voltage_throttle.h

#include <math.h>
#include <ArduinoJson.h>

#include "emonesp.h"
#include "voltage_throttle.h"
#include "app_config.h"
#include "event.h"
#include "debug.h"

VoltageThrottleTask voltageThrottle; // global instance

VoltageThrottleTask::VoltageThrottleTask() :
  MicroTasks::Task(),
  _evse(nullptr),
  _window(),
  _windowStart(0),
  _limit(VOLTAGE_THROTTLE_NO_LIMIT),
  _lastMean(NAN)
{
}

void VoltageThrottleTask::begin(EvseManager &evse)
{
  _evse = &evse;
  MicroTask.startTask(this);
}

void VoltageThrottleTask::setup()
{
  DBUGLN("Voltage throttle setup");
}

void VoltageThrottleTask::notifyConfigChanged()
{
  MicroTask.wakeTask(this);
}

uint32_t VoltageThrottleTask::getIntervalMs()
{
  uint32_t interval = voltage_throttle_interval;
  if(interval < VOLTAGE_THROTTLE_INTERVAL_MIN) {
    interval = VOLTAGE_THROTTLE_INTERVAL_MIN;
  } else if(interval > VOLTAGE_THROTTLE_INTERVAL_MAX) {
    interval = VOLTAGE_THROTTLE_INTERVAL_MAX;
  }
  return interval * 1000;
}

VoltageThrottleConfig VoltageThrottleTask::getConfig()
{
  VoltageThrottleConfig config;
  config.low = voltage_throttle_low;
  // A band of at least 1 V, whatever was configured
  config.high = voltage_throttle_high > voltage_throttle_low ? voltage_throttle_high : voltage_throttle_low + 1;
  config.step = voltage_throttle_step < 1 ? 1 :
                voltage_throttle_step > VOLTAGE_THROTTLE_STEP_MAX ? VOLTAGE_THROTTLE_STEP_MAX :
                voltage_throttle_step;
  config.min_current = (uint32_t)_evse->getMinCurrent();
  config.max_current = (uint32_t)_evse->getMaxConfiguredCurrent();
  return config;
}

unsigned long VoltageThrottleTask::loop(MicroTasks::WakeReason reason)
{
  if(nullptr == _evse) {
    return MicroTask.Infinate;
  }

  uint32_t now = millis();

  if(!voltage_throttle_enabled)
  {
    setLimit(VOLTAGE_THROTTLE_NO_LIMIT);
    _window.reset();
    _windowStart = 0;
    return MicroTask.Infinate;
  }

  // Only a charging session is throttled; the next one starts unlimited and
  // steps down again if the voltage is still low
  if(!_evse->isCharging())
  {
    setLimit(VOLTAGE_THROTTLE_NO_LIMIT);
    _window.reset();
    _windowStart = 0;
    return VOLTAGE_THROTTLE_SAMPLE_TIME;
  }

  if(0 == _windowStart) {
    _windowStart = 0 == now ? 1 : now;
  }

  // The configured voltage is not a measurement, only use live values
  if(_evse->hasLiveVoltage()) {
    _window.add(_evse->getVoltage());
  }

  if((uint32_t)(now - _windowStart) >= getIntervalMs())
  {
    decide();
    _window.reset();
    _windowStart = 0 == now ? 1 : now;
  }

  return VOLTAGE_THROTTLE_SAMPLE_TIME;
}

void VoltageThrottleTask::decide()
{
  _lastMean = _window.count > 0 ? _window.mean() : NAN;

  uint32_t limit = voltage_throttle_next_limit(_limit, (uint32_t)_evse->getPilot(), _evse->getAmps(),
                                               _window, getConfig());

  DBUGF("Voltage throttle: %u samples, mean %.1fV, min %.1fV, limit %d -> %d",
        _window.count, _window.mean(), _window.min,
        VOLTAGE_THROTTLE_NO_LIMIT == _limit ? -1 : (int)_limit,
        VOLTAGE_THROTTLE_NO_LIMIT == limit ? -1 : (int)limit);

  setLimit(limit);
}

void VoltageThrottleTask::setLimit(uint32_t limit)
{
  if(limit == _limit) {
    return;
  }

  _limit = limit;
  if(VOLTAGE_THROTTLE_NO_LIMIT == limit) {
    _evse->clearVoltageThrottleCurrent();
  } else {
    _evse->setVoltageThrottleCurrent(limit);
  }

  JsonDocument event;
  event["voltage_throttle_active"] = isThrottling() ? 1 : 0;
  event["voltage_throttle_current"] = getLimit();
  if(!isnan(_lastMean)) {
    event["voltage_throttle_voltage"] = _lastMean;
  }
  event_send(event);
}
