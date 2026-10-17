// Modbus TCP energy meter on the charger circuit, see modbus_meter.h

#include <math.h>
#include <ArduinoJson.h>

#include "emonesp.h"
#include "modbus_meter.h"
#include "modbus_meter_parser.h"
#include "net_manager.h"
#include "input.h"
#include "event.h"
#include "debug.h"

#define MODBUS_METER_UIP_REGS    6 // U, I, P floats
#define MODBUS_METER_ENERGY_REGS 2 // ImpEp float

// Plausibility limits for a single phase charger circuit
#define MODBUS_METER_VOLTAGE_MIN 50.0
#define MODBUS_METER_VOLTAGE_MAX 300.0
#define MODBUS_METER_CURRENT_MAX 100.0

ModbusMeterTask modbus_meter; // global instance

ModbusMeterTask::ModbusMeterTask() :
  MicroTasks::Task(),
  _conn(nullptr),
  _connected(false),
  _connHost(),
  _connPort(0),
  _step(Step::Idle),
  _tid(0),
  _stepStart(0),
  _lastPoll(0),
  _rdVoltage(NAN),
  _rdCurrent(NAN),
  _rdPower(NAN),
  _voltage(NAN),
  _current(NAN),
  _power(NAN),
  _energy(NAN),
  _lastUpdate(0),
  _active(false),
  _appliedVoltage(NAN),
  _errors(0),
  _lastError(nullptr)
{
}

void ModbusMeterTask::begin()
{
  MicroTask.startTask(this);
}

void ModbusMeterTask::setup()
{
  DBUGLN("Modbus meter setup");
}

unsigned long ModbusMeterTask::loop(MicroTasks::WakeReason reason)
{
  if(!modbus_meter_enabled || 0 == modbus_meter_host.length()) {
    stop();
    return MicroTask.Infinate;
  }

  uint32_t now = millis();

  // Restore the default voltage / current once the meter data is too old
  if(_active && (uint32_t)(now - _lastUpdate) >= getTimeoutMs()) {
    revert("no data from meter");
  }

  if(!net.isConnected()) {
    closeConnection();
    _step = Step::Idle;
    return 1000;
  }

  if(isConfigChanged()) {
    DBUGLN("Modbus meter: gateway changed, reconnecting");
    closeConnection();
    _step = Step::Idle;
    _lastPoll = 0;
  }

  // A request (or the connection) is in flight, the answer is handled by the
  // Mongoose event handler. Only watch for it taking too long.
  if(Step::Idle != _step)
  {
    if((uint32_t)(now - _stepStart) < MODBUS_METER_RESPONSE_TIMEOUT) {
      return 100;
    }
    fail(Step::Connecting == _step ? "connect timeout" : "response timeout", true);
  }

  uint32_t interval = getIntervalMs();
  uint32_t since = now - _lastPoll;
  if(0 == _lastPoll || since >= interval)
  {
    _lastPoll = 0 == now ? 1 : now;
    startReading();
    return 100;
  }

  uint32_t wait = interval - since;
  return wait > 1000 ? 1000 : wait;
}

void ModbusMeterTask::stop()
{
  closeConnection();
  _step = Step::Idle;
  _lastPoll = 0;

  if(_active) {
    revert("disabled");
  }

  // Do not let a quick disable / re-enable reuse old values
  _voltage = NAN;
  _current = NAN;
  _power = NAN;
  _energy = NAN;
  _lastUpdate = 0;
}

void ModbusMeterTask::notifyConfigChanged()
{
  // Wake the task so the new settings apply without waiting for the next poll
  MicroTask.wakeTask(this);
}

bool ModbusMeterTask::isConfigChanged()
{
  // The unit id is sent with each request, only the endpoint needs a new
  // connection
  return nullptr != _conn &&
         (modbus_meter_host != _connHost || modbus_meter_port != _connPort);
}

uint32_t ModbusMeterTask::getIntervalMs()
{
  uint32_t interval = modbus_meter_interval;
  if(interval < MODBUS_METER_INTERVAL_MIN) {
    interval = MODBUS_METER_INTERVAL_MIN;
  } else if(interval > MODBUS_METER_INTERVAL_MAX) {
    interval = MODBUS_METER_INTERVAL_MAX;
  }
  return interval * 1000;
}

uint32_t ModbusMeterTask::getTimeoutMs()
{
  uint32_t timeout = modbus_meter_timeout;
  if(timeout < MODBUS_METER_TIMEOUT_MIN) {
    timeout = MODBUS_METER_TIMEOUT_MIN;
  } else if(timeout > MODBUS_METER_TIMEOUT_MAX) {
    timeout = MODBUS_METER_TIMEOUT_MAX;
  }

  // Always allow at least two missed polls, so a single slow answer does not
  // make the values flip back and forth
  uint32_t ms = timeout * 1000;
  uint32_t minimum = 2 * getIntervalMs() + MODBUS_METER_RESPONSE_TIMEOUT;
  return ms < minimum ? minimum : ms;
}

void ModbusMeterTask::startReading()
{
  if(nullptr != _conn && _connected) {
    sendRequest(Step::ReadUIP);
    return;
  }

  // A connection that never completed is replaced by a fresh attempt
  closeConnection();

  char url[96];
  snprintf(url, sizeof(url), "tcp://%s:%u", modbus_meter_host.c_str(), (unsigned)modbus_meter_port);

  _conn = mg_connect(Mongoose.getMgr(), url, eventHandler, this);
  if(nullptr == _conn) {
    fail("connect failed", false);
    return;
  }

  _connHost = modbus_meter_host;
  _connPort = modbus_meter_port;
  _step = Step::Connecting;
  _stepStart = millis();
  DBUGF("Modbus meter: connecting to %s", url);
}

bool ModbusMeterTask::sendRequest(Step step)
{
  uint16_t address = DDSU666_REG_VOLTAGE;
  uint16_t count = MODBUS_METER_UIP_REGS;
  if(Step::ReadEnergy == step) {
    address = DDSU666_REG_ENERGY;
    count = MODBUS_METER_ENERGY_REGS;
  }

  uint8_t buf[MODBUS_TCP_READ_REQUEST_LEN];
  _tid++;
  size_t len = modbus_tcp_build_read(buf, sizeof(buf), _tid, (uint8_t)modbus_meter_unit, address, count);

  if(nullptr == _conn || 0 == len || !mg_send(_conn, buf, len)) {
    fail("send failed", true);
    return false;
  }

  _step = step;
  _stepStart = millis();
  return true;
}

void ModbusMeterTask::eventHandler(struct mg_connection *c, int ev, void *ev_data)
{
  ModbusMeterTask *self = static_cast<ModbusMeterTask *>(c->fn_data);
  if(nullptr != self) {
    self->onEvent(c, ev, ev_data);
  }
}

void ModbusMeterTask::onEvent(struct mg_connection *c, int ev, void *ev_data)
{
  switch(ev)
  {
    case MG_EV_CONNECT:
      DBUGLN("Modbus meter: connected");
      _connected = true;
      if(Step::Connecting == _step) {
        sendRequest(Step::ReadUIP);
      }
      break;

    case MG_EV_READ:
      onReceive(c);
      break;

    case MG_EV_ERROR:
      // Mongoose closes the connection next, MG_EV_CLOSE does the clean up
      DBUGF("Modbus meter: %s", ev_data ? (const char *)ev_data : "error");
      break;

    case MG_EV_CLOSE:
      if(c == _conn)
      {
        DBUGLN("Modbus meter: connection closed");
        _conn = nullptr;
        _connected = false;
        if(Step::Idle != _step) {
          fail("connection closed", false);
        }
      }
      break;
  }
}

void ModbusMeterTask::onReceive(struct mg_connection *c)
{
  while(c->recv.len > 0 && (Step::ReadUIP == _step || Step::ReadEnergy == _step))
  {
    uint16_t count = Step::ReadUIP == _step ? MODBUS_METER_UIP_REGS : MODBUS_METER_ENERGY_REGS;
    uint16_t regs[MODBUS_METER_UIP_REGS];
    size_t consumed = 0;
    uint8_t exception_code = 0;

    ModbusParseResult res = modbus_tcp_parse_read(c->recv.buf, c->recv.len, _tid, count,
                                                  regs, consumed, exception_code);
    if(ModbusParseResult::NeedMore == res) {
      return;
    }

    if(ModbusParseResult::BadFrame == res) {
      // Most likely not a Modbus TCP gateway (e.g. RTU over TCP)
      mg_iobuf_del(&c->recv, 0, c->recv.len);
      fail("invalid response", true);
      return;
    }

    mg_iobuf_del(&c->recv, 0, consumed);

    if(ModbusParseResult::WrongTransaction == res) {
      // late answer to a request that already timed out
      continue;
    }

    if(ModbusParseResult::Exception == res)
    {
      DBUGF("Modbus meter: exception %u", exception_code);
      if(Step::ReadEnergy == _step) {
        // U / I / P are still good, only the energy is missing
        completeReading(NAN);
      } else {
        fail("modbus exception", false);
      }
      continue;
    }

    // ModbusParseResult::Ok
    if(Step::ReadUIP == _step)
    {
      _rdVoltage = modbus_regs_to_float(&regs[0]);
      _rdCurrent = fabs(modbus_regs_to_float(&regs[2]));
      _rdPower = modbus_regs_to_float(&regs[4]) * 1000.0; // kW -> W
      sendRequest(Step::ReadEnergy);
    } else {
      completeReading(modbus_regs_to_float(&regs[0]));
    }
  }

  // Nothing is expected while idle
  if(Step::Idle == _step && c->recv.len > 0) {
    mg_iobuf_del(&c->recv, 0, c->recv.len);
  }
}

void ModbusMeterTask::completeReading(double energy)
{
  _step = Step::Idle;

  if(isnan(_rdVoltage) || isinf(_rdVoltage) ||
     _rdVoltage < MODBUS_METER_VOLTAGE_MIN || _rdVoltage > MODBUS_METER_VOLTAGE_MAX ||
     isnan(_rdCurrent) || isinf(_rdCurrent) || _rdCurrent > MODBUS_METER_CURRENT_MAX ||
     isnan(_rdPower) || isinf(_rdPower))
  {
    DBUGF("Modbus meter: implausible values U=%.1f I=%.2f P=%.0f", _rdVoltage, _rdCurrent, _rdPower);
    fail("implausible values", false);
    return;
  }

  _voltage = _rdVoltage;
  _current = _rdCurrent;
  _power = _rdPower;
  if(!isnan(energy) && !isinf(energy) && energy >= 0) {
    _energy = energy;
  }

  _lastUpdate = millis();
  if(0 == _lastUpdate) {
    _lastUpdate = 1;
  }
  _lastError = nullptr;

  DBUGF("Modbus meter: U=%.1fV I=%.2fA P=%.0fW E=%.2fkWh", _voltage, _current, _power, _energy);
  apply();
}

void ModbusMeterTask::fail(const char *reason, bool closeConn)
{
  _errors++;
  _lastError = reason;
  _step = Step::Idle;
  DBUGF("Modbus meter: %s", reason);

  if(closeConn) {
    closeConnection();
  }
}

void ModbusMeterTask::closeConnection()
{
  if(nullptr != _conn)
  {
    // Detach first: the close event arrives on a later poll, after which
    // nothing may call back into this task for this connection
    _conn->fn_data = nullptr;
    _conn->is_closing = 1;
    _conn = nullptr;
  }
  _connected = false;
}

void ModbusMeterTask::apply()
{
  // Every voltage change is forwarded to the controller over RAPI, so ignore
  // sub-volt jitter. Re-sending the same value keeps it fresh without RAPI
  // traffic.
  if(isnan(_appliedVoltage) || fabs(_voltage - _appliedVoltage) >= MODBUS_METER_VOLTAGE_STEP) {
    _appliedVoltage = _voltage;
  }
  evse.setMqttVoltage(_appliedVoltage);
  evse.setExternalCurrent(_current);

  if(!_active) {
    _active = true;
    DBUGLN("Modbus meter: meter values active");
  }

  sendStatusEvent();
}

void ModbusMeterTask::revert(const char *reason)
{
  _active = false;
  _appliedVoltage = NAN;

  evse.clearExternalCurrent();
  evse.clearMqttVoltage();

  DBUGF("Modbus meter: %s, back to default voltage / current", reason);
  sendStatusEvent();
}

void ModbusMeterTask::sendStatusEvent()
{
  JsonDocument doc;
  doc["modbus_meter_connected"] = _connected ? 1 : 0;
  doc["modbus_meter_active"] = _active ? 1 : 0;
  doc["modbus_meter_data_age"] = getDataAge();
  doc["modbus_meter_voltage"] = _voltage;
  doc["modbus_meter_current"] = _current;
  doc["modbus_meter_power"] = _power;
  doc["modbus_meter_energy"] = _energy;
  doc["modbus_meter_errors"] = _errors;
  event_send(doc);
}
