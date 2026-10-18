// Voltage drop between the supply entrance and the charger, see voltage_drop.h

#include <math.h>

#include "emonesp.h"
#include "voltage_drop.h"
#include "modbus_meter.h"
#include "modbus_grid.h"
#include "event.h"
#include "debug.h"

VoltageDropTask voltageDrop; // global instance

VoltageDropTask::VoltageDropTask() :
  MicroTasks::Task(),
  _evse(nullptr),
  _estimator(),
  _available(false),
  _gridVoltage(NAN),
  _chargerVoltage(NAN),
  _amps(NAN),
  _lastEvent(0)
{
}

void VoltageDropTask::begin(EvseManager &evse)
{
  _evse = &evse;
  MicroTask.startTask(this);
}

void VoltageDropTask::setup()
{
  DBUGLN("Voltage drop setup");
}

unsigned long VoltageDropTask::loop(MicroTasks::WakeReason reason)
{
  bool available =
    modbus_meter_enabled && modbus_grid_enabled && modbus_grid.isCurrentMode() &&
    modbus_meter.getDataAge() <= VOLTAGE_DROP_MAX_AGE &&
    modbus_grid.getDataAge() <= VOLTAGE_DROP_MAX_AGE;

  if(available)
  {
    _gridVoltage = modbus_grid.getVoltage();
    _chargerVoltage = modbus_meter.getVoltage();
    _amps = modbus_meter.getCurrent();
    voltage_drop_add_sample(_estimator, _gridVoltage, _chargerVoltage, _amps);
  }
  else
  {
    // The next pair must not be compared with one from before the gap
    _estimator.hasLast = false;
  }

  uint32_t now = millis();
  if(available != _available ||
     (available && (uint32_t)(now - _lastEvent) >= VOLTAGE_DROP_EVENT_TIME))
  {
    _available = available;
    _lastEvent = now;
    sendStatusEvent();
  }

  return VOLTAGE_DROP_SAMPLE_TIME;
}

void VoltageDropTask::addStatus(JsonDocument &doc)
{
  if(!_available) {
    return;
  }

  double drop = voltage_drop_now(_estimator, _gridVoltage, _chargerVoltage);
  doc["voltage_drop"] = drop;
  doc["voltage_drop_pct"] = _gridVoltage > 0 ? drop * 100.0 / _gridVoltage : 0;
  doc["voltage_drop_grid_voltage"] = _gridVoltage;
  doc["voltage_drop_charger_voltage"] = _chargerVoltage;
  if(voltage_drop_has_offset(_estimator)) {
    doc["voltage_drop_offset"] = _estimator.offset;
  }
  if(voltage_drop_has_resistance(_estimator))
  {
    double ohms = _estimator.ohms;
    uint32_t max = _evse ? _evse->getMaxConfiguredCurrent() : 0;
    doc["voltage_drop_resistance"] = ohms;
    doc["voltage_drop_loss"] = _amps > 0 ? _amps * _amps * ohms : 0;
    doc["voltage_drop_max_current"] = max;
    doc["voltage_drop_at_max"] = ohms * max;
  }
}

void VoltageDropTask::sendStatusEvent()
{
  JsonDocument doc;
  doc["voltage_drop_available"] = _available ? 1 : 0;
  addStatus(doc);
  event_send(doc);
}
