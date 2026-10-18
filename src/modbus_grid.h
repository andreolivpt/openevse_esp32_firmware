#ifndef _MODBUS_GRID_H
#define _MODBUS_GRID_H

// Grid power over Modbus TCP for the Load Shaper
//
// Periodically reads the whole-house grid supply from a Modbus TCP device and
// feeds it to the Load Shaper, in place of the MQTT live power topic.
//
// modbus_grid_mode:
//  "register"  one configurable register holding the grid power (W),
//              given to the shaper as its live power (defaults match an
//              AlphaESS: unit 85, register 0x0021, int32, positive = import)
//  "alphaess"  the AlphaESS grid meter block (voltage, current, active and
//              apparent power) in one read; the shaper gets the grid current
//              and voltage and limits in amps (current_shaper_max_cur /
//              current_shaper_max_va)
//
// Nothing is sent when a read fails: the Load Shaper's own data timeout
// (current_shaper_data_maxinterval, and current_shaper_release_on_timeout)
// decides what happens when the readings stop.

#ifndef MODBUS_GRID_LOOP_TIME
#define MODBUS_GRID_LOOP_TIME 200 // ms
#endif

#define MODBUS_GRID_INTERVAL_MIN 1  // s
#define MODBUS_GRID_INTERVAL_MAX 60 // s

#include <Arduino.h>
#include <MicroTasks.h>

#include "modbus_tcp_client.h"
#include "modbus_meter_parser.h"
#include "app_config.h"

class ModbusGridTask : public MicroTasks::Task
{
  private:
    ModbusTcpClient _client;
    uint32_t _lastPoll;
    uint32_t _lastUpdate;
    uint32_t _lastEvent;
    double _power;    // W
    double _voltage;  // V, alphaess mode
    double _current;  // A, alphaess mode, negative while exporting
    double _apparent; // VA, alphaess mode
    uint32_t _errors;
    const char *_lastError;
    bool _running;

    void startRead();
    void startRegisterRead();
    void startAlphaessRead();
    void onRead(bool ok, const char *error);
    void clearReadings();
    void sendStatusEvent();
    uint32_t getIntervalMs();

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    ModbusGridTask();

    void begin();
    void stop();
    void notifyConfigChanged();

    bool isConnected() {
      return _client.isConnected();
    }

    double getPower() {
      return _power;
    }

    bool isCurrentMode() {
      return modbus_grid_mode == "alphaess";
    }

    double getVoltage() {
      return _voltage;
    }

    double getCurrent() {
      return _current;
    }

    double getApparent() {
      return _apparent;
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

extern ModbusGridTask modbus_grid;

#endif // _MODBUS_GRID_H
