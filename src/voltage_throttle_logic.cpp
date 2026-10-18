// Decision logic for the low voltage throttle, see voltage_throttle_logic.h

#include <math.h>

#include "voltage_throttle_logic.h"

uint32_t voltage_throttle_next_limit(uint32_t limit, uint32_t pilot, double amps,
                                     const VoltageThrottleWindow &window,
                                     const VoltageThrottleConfig &config)
{
  if(0 == window.count) {
    return VOLTAGE_THROTTLE_NO_LIMIT;
  }

  uint32_t step = config.step > 0 ? config.step : 1;

  if(window.mean() < config.low)
  {
    // Step down from what is really in use: the limit (or the pilot when not
    // limiting yet), or the vehicle's own draw if that is lower
    uint32_t base = VOLTAGE_THROTTLE_NO_LIMIT == limit ? pilot : limit;
    if(config.max_current > 0 && base > config.max_current) {
      base = config.max_current;
    }
    if(amps > 0)
    {
      long drawn = lround(amps);
      if(drawn >= 0 && (uint32_t)drawn < base) {
        base = (uint32_t)drawn;
      }
    }

    if(base <= config.min_current + step) {
      return config.min_current;
    }
    return base - step;
  }

  if(VOLTAGE_THROTTLE_NO_LIMIT == limit) {
    return VOLTAGE_THROTTLE_NO_LIMIT;
  }

  if(window.min > config.high)
  {
    // The vehicle takes clearly less than the limit (it is limiting itself),
    // so the limit no longer does anything: drop it rather than step up to
    // the maximum one interval at a time. Should the voltage fall again, the
    // next step down starts from what the vehicle draws.
    if(amps > 0 && lround(amps) + 1 < (long)limit) {
      return VOLTAGE_THROTTLE_NO_LIMIT;
    }

    uint32_t next = limit + step;
    if(next >= config.max_current) {
      return VOLTAGE_THROTTLE_NO_LIMIT;
    }
    return next;
  }

  return limit;
}
