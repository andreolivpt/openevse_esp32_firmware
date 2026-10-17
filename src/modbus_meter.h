#ifndef _MODBUS_METER_H
#define _MODBUS_METER_H

// Modbus TCP energy meter on the charger circuit (CHINT DDSU666 behind a
// Modbus TCP gateway).
//
// Periodically reads voltage, current, active power and imported energy.
// While readings are fresh they replace:
//  - the voltage (same path as the mqtt_vrms feed, which is not subscribed to
//    while this source is enabled)
//  - the charging current reported by the OpenEVSE controller, so power and
//    the session / total energy counters follow the meter
//
// When no valid reading has arrived for `modbus_meter_timeout` seconds, or
// the source is disabled, the default behaviour is restored (configured
// voltage, controller current).
//
// Networking goes through Mongoose (non-blocking), so a slow or unreachable
// gateway never stalls the main loop.

#ifndef MODBUS_METER_RESPONSE_TIMEOUT
#define MODBUS_METER_RESPONSE_TIMEOUT 3000 // ms, max wait for connect / answer
#endif

#ifndef MODBUS_METER_VOLTAGE_STEP
#define MODBUS_METER_VOLTAGE_STEP 1.0 // V, smaller changes are not pushed to the controller
#endif

#define MODBUS_METER_INTERVAL_MIN 1    // s
#define MODBUS_METER_INTERVAL_MAX 30   // s
#define MODBUS_METER_TIMEOUT_MIN  10   // s
// The voltage path (EvseMonitor::setMqttVoltage) expires on its own after
// EVSE_MONITOR_MQTT_VOLTAGE_TIMEOUT_MS (120 s), so a longer timeout here
// would only delay the current fallback.
#define MODBUS_METER_TIMEOUT_MAX  120  // s

// DDSU666 holding registers, IEEE 754 floats, high word first
#define DDSU666_REG_VOLTAGE 0x2000 // U  (V), followed by I (A) and P (kW)
#define DDSU666_REG_ENERGY  0x4000 // ImpEp (kWh)

#include <Arduino.h>
#include <MicroTasks.h>
#include <MongooseCore.h>

#include "app_config.h"

class ModbusMeterTask : public MicroTasks::Task
{
  private:
    enum class Step : uint8_t {
      Idle,       // nothing in flight
      Connecting, // waiting for the TCP connection
      ReadUIP,    // waiting for voltage / current / power
      ReadEnergy  // waiting for the energy register
    };

    struct mg_connection *_conn;
    bool _connected;
    String _connHost;
    uint16_t _connPort;

    Step _step;
    uint16_t _tid;
    uint32_t _stepStart;
    uint32_t _lastPoll;

    // values of the reading in progress
    double _rdVoltage;
    double _rdCurrent;
    double _rdPower;

    // last valid reading
    double _voltage;
    double _current;
    double _power;  // W
    double _energy; // kWh, imported
    uint32_t _lastUpdate;

    // values currently applied to the EVSE
    bool _active;
    double _appliedVoltage;

    uint32_t _errors;
    const char *_lastError;

    static void eventHandler(struct mg_connection *c, int ev, void *ev_data);
    void onEvent(struct mg_connection *c, int ev, void *ev_data);
    void onReceive(struct mg_connection *c);

    bool isConfigChanged();
    void startReading();
    bool sendRequest(Step step);
    void completeReading(double energy);
    void fail(const char *reason, bool closeConn);
    void closeConnection();
    void apply();
    void revert(const char *reason);
    void sendStatusEvent();

    uint32_t getIntervalMs();
    uint32_t getTimeoutMs();

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    ModbusMeterTask();

    void begin();
    void stop();
    void notifyConfigChanged();

    bool isConnected() {
      return _connected;
    }

    // true while the meter values replace the default ones
    bool isActive() {
      return _active;
    }

    double getVoltage() {
      return _voltage;
    }

    double getCurrent() {
      return _current;
    }

    double getPower() {
      return _power;
    }

    double getEnergy() {
      return _energy;
    }

    uint32_t getErrors() {
      return _errors;
    }

    const char *getLastError() {
      return _lastError;
    }

    uint32_t getDataAge() {
      return 0 == _lastUpdate ? UINT32_MAX : millis() - _lastUpdate;
    }
};

extern ModbusMeterTask modbus_meter;

#endif // _MODBUS_METER_H
