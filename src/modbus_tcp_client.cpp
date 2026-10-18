// Small asynchronous Modbus TCP client, see modbus_tcp_client.h

#include "emonesp.h"
#include "modbus_tcp_client.h"
#include "modbus_meter_parser.h"
#include "debug.h"

ModbusTcpClient::ModbusTcpClient() :
  _conn(nullptr),
  _connected(false),
  _host(),
  _port(0),
  _unit(1),
  _step(Step::Idle),
  _stepStart(0),
  _tid(0),
  _address(0),
  _count(0),
  _callback(nullptr)
{
}

ModbusTcpClient::~ModbusTcpClient()
{
  close();
}

void ModbusTcpClient::setEndpoint(const String &host, uint16_t port, uint8_t unit)
{
  if(host != _host || port != _port) {
    close();
  }
  _host = host;
  _port = port;
  _unit = unit;
}

bool ModbusTcpClient::read(uint16_t address, uint16_t count, ReadCallback callback)
{
  if(Step::Idle != _step || 0 == count || count > MODBUS_TCP_CLIENT_MAX_REGS ||
     0 == _host.length() || !callback)
  {
    return false;
  }

  _address = address;
  _count = count;
  _callback = callback;
  _stepStart = millis();

  if(nullptr != _conn && _connected) {
    // A send failure is reported through the callback
    sendRequest();
    return true;
  }

  // A connection that never completed is replaced by a fresh attempt
  close();

  char url[96];
  snprintf(url, sizeof(url), "tcp://%s:%u", _host.c_str(), (unsigned)_port);
  _conn = mg_connect(Mongoose.getMgr(), url, eventHandler, this);
  if(nullptr == _conn) {
    finish(false, nullptr, "connect failed", false);
    return true;
  }

  _step = Step::Connecting;
  return true;
}

bool ModbusTcpClient::sendRequest()
{
  uint8_t buf[MODBUS_TCP_READ_REQUEST_LEN];
  _tid++;
  size_t len = modbus_tcp_build_read(buf, sizeof(buf), _tid, _unit, _address, _count);
  if(nullptr == _conn || 0 == len || !mg_send(_conn, buf, len)) {
    finish(false, nullptr, "send failed", true);
    return false;
  }
  _step = Step::Waiting;
  _stepStart = millis();
  return true;
}

void ModbusTcpClient::poll()
{
  if(Step::Idle != _step && (uint32_t)(millis() - _stepStart) >= MODBUS_TCP_CLIENT_TIMEOUT) {
    finish(false, nullptr, Step::Connecting == _step ? "connect timeout" : "response timeout", true);
  }
}

void ModbusTcpClient::close()
{
  if(nullptr != _conn)
  {
    // Detach first: the close event arrives on a later poll
    _conn->fn_data = nullptr;
    _conn->is_closing = 1;
    _conn = nullptr;
  }
  _connected = false;
}

void ModbusTcpClient::finish(bool ok, const uint16_t *regs, const char *error, bool closeConn)
{
  _step = Step::Idle;
  if(closeConn) {
    close();
  }

  // The callback may start the next read, so hand it over first
  ReadCallback callback = _callback;
  _callback = nullptr;
  if(callback) {
    callback(ok, regs, ok ? _count : 0, error);
  }
}

void ModbusTcpClient::eventHandler(struct mg_connection *c, int ev, void *ev_data)
{
  ModbusTcpClient *self = static_cast<ModbusTcpClient *>(c->fn_data);
  if(nullptr != self) {
    self->onEvent(c, ev, ev_data);
  }
}

void ModbusTcpClient::onEvent(struct mg_connection *c, int ev, void *ev_data)
{
  switch(ev)
  {
    case MG_EV_CONNECT:
      _connected = true;
      if(Step::Connecting == _step) {
        sendRequest();
      }
      break;

    case MG_EV_READ:
      onReceive(c);
      break;

    case MG_EV_ERROR:
      DBUGF("Modbus TCP %s: %s", _host.c_str(), ev_data ? (const char *)ev_data : "error");
      break;

    case MG_EV_CLOSE:
      if(c == _conn)
      {
        _conn = nullptr;
        _connected = false;
        if(Step::Idle != _step) {
          finish(false, nullptr, "connection closed", false);
        }
      }
      break;
  }
}

void ModbusTcpClient::onReceive(struct mg_connection *c)
{
  while(c->recv.len > 0 && Step::Waiting == _step)
  {
    uint16_t regs[MODBUS_TCP_CLIENT_MAX_REGS];
    size_t consumed = 0;
    uint8_t exception_code = 0;

    ModbusParseResult res = modbus_tcp_parse_read(c->recv.buf, c->recv.len, _tid, _count,
                                                  regs, consumed, exception_code);
    if(ModbusParseResult::NeedMore == res) {
      return;
    }

    if(ModbusParseResult::BadFrame == res) {
      mg_iobuf_del(&c->recv, 0, c->recv.len);
      finish(false, nullptr, "invalid response", true);
      return;
    }

    mg_iobuf_del(&c->recv, 0, consumed);

    if(ModbusParseResult::WrongTransaction == res) {
      continue; // late answer to an earlier request
    }

    if(ModbusParseResult::Exception == res) {
      // e.g. 6 = device busy: keep the connection, the next read retries
      finish(false, nullptr, "modbus exception", false);
      return;
    }

    finish(true, regs, nullptr, false);
    return;
  }

  // Nothing is expected while idle
  if(Step::Idle == _step && c->recv.len > 0) {
    mg_iobuf_del(&c->recv, 0, c->recv.len);
  }
}
