#ifndef _MODBUS_GRID_H
#define _MODBUS_GRID_H

// Grid power over Modbus TCP for the Load Shaper
//
// Periodically reads the whole-house grid power from a Modbus TCP device
// (defaults match an AlphaESS inverter: unit 85, holding register 0x0021,
// int32 W, positive = importing) and feeds it to the Load Shaper as its live
// power, in place of the MQTT live power topic.
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

class ModbusGridTask : public MicroTasks::Task
{
  private:
    ModbusTcpClient _client;
    uint32_t _lastPoll;
    uint32_t _lastUpdate;
    uint32_t _lastEvent;
    double _power;
    uint32_t _errors;
    const char *_lastError;
    bool _running;

    void startRead();
    void onRead(bool ok, const uint16_t *regs, uint16_t count, const char *error);
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
