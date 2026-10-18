#ifndef _VOLTAGE_DROP_LOGIC_H
#define _VOLTAGE_DROP_LOGIC_H

// Voltage drop between the supply entrance and the charger.
//
// Two meters are compared: one at the entrance (the AlphaESS grid meter) and
// one on the charger circuit (the Modbus TCP meter). Their difference is the
// drop along the cable between them plus a fixed calibration offset between
// the two meters, so:
//
//   drop = offset + R * amps
//
//  - offset is learnt while no current flows (amps below
//    VOLTAGE_DROP_IDLE_AMPS): there the whole difference is the offset
//  - R, the resistance of the run, is learnt while charging (amps of at
//    least VOLTAGE_DROP_LOAD_AMPS): (difference - offset) / amps
//
// Both are slow moving averages, so the readings of the two meters, taken a
// few seconds apart and the entrance one in whole volts, average out.
// Samples taken while the current is changing are skipped.
//
// Header only and dependency free, unit tested on the host
// (test/test_modbus_meter).

#define VOLTAGE_DROP_IDLE_AMPS      0.5  // A, below: no load on the run
#define VOLTAGE_DROP_LOAD_AMPS      6.0  // A, from: enough load to see R
#define VOLTAGE_DROP_MAX_STEP_AMPS  1.0  // A, current change that skips a sample
#define VOLTAGE_DROP_AVERAGE        60   // samples, the averages' time constant
#define VOLTAGE_DROP_MIN_IDLE       12   // idle samples before the offset is used
#define VOLTAGE_DROP_MIN_LOAD       6    // load samples before R is reported
#define VOLTAGE_DROP_MAX_OHMS       2.0  // anything above is a measurement error
#define VOLTAGE_DROP_MAX_DIFF       40.0 // V, larger differences are rejected

struct VoltageDropEstimator
{
  double offset = 0;       // V, entrance meter minus charger meter at no load
  unsigned long idle = 0;  // idle samples averaged into offset
  double ohms = 0;         // resistance of the run
  unsigned long load = 0;  // load samples averaged into ohms
  double lastAmps = 0;
  bool hasLast = false;
};

// Moving average that starts as a plain mean, so the first samples count
// fully, then settles on a time constant of VOLTAGE_DROP_AVERAGE samples.
inline double voltage_drop_average(double average, unsigned long count, double sample)
{
  unsigned long n = count + 1;
  if(n > VOLTAGE_DROP_AVERAGE) {
    n = VOLTAGE_DROP_AVERAGE;
  }
  return average + (sample - average) / (double)n;
}

// Add one pair of readings. Returns true when it was used.
inline bool voltage_drop_add_sample(VoltageDropEstimator &e, double grid_v, double charger_v, double amps)
{
  // NaN compares false: rejected along with the out of range values
  if(!(grid_v > 0) || !(charger_v > 0) || !(amps >= 0)) {
    return false;
  }

  bool steady = e.hasLast && (amps - e.lastAmps <= VOLTAGE_DROP_MAX_STEP_AMPS &&
                              e.lastAmps - amps <= VOLTAGE_DROP_MAX_STEP_AMPS);
  e.lastAmps = amps;
  e.hasLast = true;
  if(!steady) {
    return false;
  }

  double diff = grid_v - charger_v;
  if(diff > VOLTAGE_DROP_MAX_DIFF || diff < -VOLTAGE_DROP_MAX_DIFF) {
    return false;
  }

  if(amps < VOLTAGE_DROP_IDLE_AMPS)
  {
    e.offset = voltage_drop_average(e.offset, e.idle, diff);
    e.idle++;
    return true;
  }

  if(amps >= VOLTAGE_DROP_LOAD_AMPS && e.idle >= VOLTAGE_DROP_MIN_IDLE)
  {
    double ohms = (diff - e.offset) / amps;
    if(ohms < 0) {
      ohms = 0; // meter noise around a very short run
    }
    if(ohms > VOLTAGE_DROP_MAX_OHMS) {
      return false;
    }
    e.ohms = voltage_drop_average(e.ohms, e.load, ohms);
    e.load++;
    return true;
  }

  return false;
}

inline bool voltage_drop_has_offset(const VoltageDropEstimator &e)
{
  return e.idle >= VOLTAGE_DROP_MIN_IDLE;
}

inline bool voltage_drop_has_resistance(const VoltageDropEstimator &e)
{
  return voltage_drop_has_offset(e) && e.load >= VOLTAGE_DROP_MIN_LOAD;
}

// Drop along the run right now: the difference between the meters, less the
// calibration offset once it is known.
inline double voltage_drop_now(const VoltageDropEstimator &e, double grid_v, double charger_v)
{
  double diff = grid_v - charger_v;
  return voltage_drop_has_offset(e) ? diff - e.offset : diff;
}

#endif // _VOLTAGE_DROP_LOGIC_H
