#ifndef _VOLTAGE_DROP_H
#define _VOLTAGE_DROP_H

// Voltage drop between the supply entrance and the charger, from the
// AlphaESS grid meter (Modbus grid source in alphaess mode) and the Modbus TCP
// meter on the charger circuit. Reporting only, nothing is controlled from
// it. See voltage_drop_logic.h for the estimation.

#ifndef VOLTAGE_DROP_SAMPLE_TIME
#define VOLTAGE_DROP_SAMPLE_TIME 5000 // ms
#endif

#ifndef VOLTAGE_DROP_EVENT_TIME
#define VOLTAGE_DROP_EVENT_TIME 30000 // ms
#endif

// A reading older than this is not paired with the other meter's
#define VOLTAGE_DROP_MAX_AGE 8000 // ms

#include <Arduino.h>
#include <ArduinoJson.h>
#include <MicroTasks.h>

#include "evse_man.h"
#include "voltage_drop_logic.h"

class VoltageDropTask : public MicroTasks::Task
{
  private:
    EvseManager *_evse;
    VoltageDropEstimator _estimator;
    bool _available;   // both meters have fresh readings
    double _gridVoltage;
    double _chargerVoltage;
    double _amps;
    uint32_t _lastEvent;

    void sendStatusEvent();

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    VoltageDropTask();

    void begin(EvseManager &evse);

    // Both meters have fresh readings
    bool isAvailable() {
      return _available;
    }

    // Add the voltage_drop_* fields to a status document (none while the
    // two meters are not both available)
    void addStatus(JsonDocument &doc);
};

extern VoltageDropTask voltageDrop;

#endif // _VOLTAGE_DROP_H
