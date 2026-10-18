#ifndef _CURRENT_SHAPER_LIMIT_H
#define _CURRENT_SHAPER_LIMIT_H

// Grid current limit for the Load Shaper when its live data is a current
// (see CurrentShaperTask::setLiveCurrent). Header only, no dependencies, so
// it can be unit tested on the host (test/test_modbus_meter).

// Current (A) the whole house may draw from the grid.
//
//  max_cur      configured current limit (A), 0 = not used
//  max_va       configured apparent power limit (VA), 0 = not used; divided
//               by the grid voltage of the moment, so a supply limited in kVA
//               is respected above as well as below the nominal voltage
//  fallback_va  used as max_va when neither limit is set (the shaper's
//               max power setting)
//  volts        grid voltage
//
// With both limits set the lower one applies. Returns 0 when no limit can be
// worked out, which pauses the charge like an exhausted supply.
inline double shaper_grid_current_limit(double max_cur, double max_va, double fallback_va, double volts)
{
  if(max_cur <= 0 && max_va <= 0) {
    max_va = fallback_va;
  }

  double limit = -1;
  if(max_cur > 0) {
    limit = max_cur;
  }
  if(max_va > 0 && volts > 0) {
    double va_limit = max_va / volts;
    if(limit < 0 || va_limit < limit) {
      limit = va_limit;
    }
  }
  return limit < 0 ? 0 : limit;
}

// Current available to the vehicle: what is left under the limit, plus what
// the vehicle already draws (it is part of the grid current).
inline double shaper_ev_current_from_grid(double limit, double grid_cur, double ev_cur)
{
  return limit - grid_cur + ev_cur;
}

#endif // _CURRENT_SHAPER_LIMIT_H
