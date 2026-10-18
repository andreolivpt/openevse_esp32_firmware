// Grid power over Modbus TCP for the Load Shaper, see modbus_grid.h

#include <math.h>
#include <ArduinoJson.h>

#include "emonesp.h"
#include "modbus_grid.h"
#include "app_config.h"
#include "net_manager.h"
#include "current_shaper.h"
#include "event.h"
#include "debug.h"

// Plausibility limit for a home supply
#define MODBUS_GRID_POWER_MAX 200000.0 // W

ModbusGridTask modbus_grid; // global instance

ModbusGridTask::ModbusGridTask() :
  MicroTasks::Task(),
  _client(),
  _lastPoll(0),
  _lastUpdate(0),
  _lastEvent(0),
  _power(NAN),
  _errors(0),
  _lastError(nullptr),
  _running(false)
{
}

void ModbusGridTask::begin()
{
  MicroTask.startTask(this);
}

void ModbusGridTask::setup()
{
  DBUGLN("Modbus grid setup");
}

void ModbusGridTask::notifyConfigChanged()
{
  _lastPoll = 0;
  MicroTask.wakeTask(this);
}

void ModbusGridTask::stop()
{
  _client.close();
  _lastPoll = 0;
  _lastUpdate = 0;
  _power = NAN;
  _running = false;
}

uint32_t ModbusGridTask::getIntervalMs()
{
  uint32_t interval = modbus_grid_interval;
  if(interval < MODBUS_GRID_INTERVAL_MIN) {
    interval = MODBUS_GRID_INTERVAL_MIN;
  } else if(interval > MODBUS_GRID_INTERVAL_MAX) {
    interval = MODBUS_GRID_INTERVAL_MAX;
  }
  return interval * 1000;
}

unsigned long ModbusGridTask::loop(MicroTasks::WakeReason reason)
{
  if(!modbus_grid_enabled || 0 == modbus_grid_host.length())
  {
    if(_running) {
      stop();
      sendStatusEvent();
    }
    return MicroTask.Infinate;
  }

  if(!_running) {
    _running = true;
    DBUGLN("Modbus grid: started");
  }

  if(!net.isConnected()) {
    _client.close();
    return 1000;
  }

  _client.setEndpoint(modbus_grid_host, modbus_grid_port, (uint8_t)modbus_grid_unit);
  _client.poll();

  if(!_client.isBusy())
  {
    uint32_t now = millis();
    if(0 == _lastPoll || (uint32_t)(now - _lastPoll) >= getIntervalMs())
    {
      _lastPoll = 0 == now ? 1 : now;
      startRead();
    }
  }

  return MODBUS_GRID_LOOP_TIME;
}

void ModbusGridTask::startRead()
{
  ModbusValueFormat format = ModbusValueFormat::Int32;
  if(!modbus_value_format_parse(modbus_grid_format.c_str(), format))
  {
    _errors++;
    _lastError = "invalid format";
    return;
  }

  _client.read(modbus_grid_register, modbus_value_format_regs(format),
    [this, format](bool ok, const uint16_t *regs, uint16_t count, const char *error)
    {
      if(!ok) {
        onRead(false, nullptr, 0, error);
        return;
      }

      double watts = modbus_decode_value(regs, format) * modbus_grid_scale;
      if(modbus_grid_invert) {
        watts = -watts;
      }

      if(isnan(watts) || isinf(watts) || fabs(watts) > MODBUS_GRID_POWER_MAX) {
        onRead(false, nullptr, 0, "implausible value");
        return;
      }

      _power = watts;
      onRead(true, regs, count, nullptr);
    });
}

void ModbusGridTask::onRead(bool ok, const uint16_t *regs, uint16_t count, const char *error)
{
  if(!ok)
  {
    _errors++;
    _lastError = error;
    DBUGF("Modbus grid: %s", error ? error : "error");
    return;
  }

  _lastUpdate = millis();
  if(0 == _lastUpdate) {
    _lastUpdate = 1;
  }
  _lastError = nullptr;

  // Same path as the MQTT live power topic and POST /status
  shaper.setLivePwr((int)lround(_power));

  // The shaper publishes its own live power on every update; keep this
  // status feed light
  if(0 == _lastEvent || (uint32_t)(_lastUpdate - _lastEvent) >= 10000) {
    _lastEvent = _lastUpdate;
    sendStatusEvent();
  }
}

void ModbusGridTask::sendStatusEvent()
{
  JsonDocument doc;
  doc["modbus_grid_connected"] = _client.isConnected() ? 1 : 0;
  doc["modbus_grid_data_age"] = getDataAge();
  doc["modbus_grid_power"] = _power;
  doc["modbus_grid_errors"] = _errors;
  event_send(doc);
}
