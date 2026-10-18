#ifndef _MODBUS_TCP_CLIENT_H
#define _MODBUS_TCP_CLIENT_H

// Small asynchronous Modbus TCP client (read holding registers only) on top
// of Mongoose. Keeps one connection open between reads, reconnects on demand
// and never blocks: the result arrives through a callback from the Mongoose
// poll, and the owner calls poll() to enforce the response timeout.

#ifndef MODBUS_TCP_CLIENT_TIMEOUT
#define MODBUS_TCP_CLIENT_TIMEOUT 3000 // ms, max wait for connect / answer
#endif

#define MODBUS_TCP_CLIENT_MAX_REGS 32

#include <Arduino.h>
#include <functional>
#include <MongooseCore.h>

class ModbusTcpClient
{
  public:
    // ok: regs/count hold the answer; otherwise error describes the failure
    typedef std::function<void(bool ok, const uint16_t *regs, uint16_t count, const char *error)> ReadCallback;

  private:
    enum class Step : uint8_t {
      Idle,
      Connecting,
      Waiting
    };

    struct mg_connection *_conn;
    bool _connected;
    String _host;
    uint16_t _port;
    uint8_t _unit;

    Step _step;
    uint32_t _stepStart;
    uint16_t _tid;
    uint16_t _address;
    uint16_t _count;
    ReadCallback _callback;

    static void eventHandler(struct mg_connection *c, int ev, void *ev_data);
    void onEvent(struct mg_connection *c, int ev, void *ev_data);
    void onReceive(struct mg_connection *c);
    bool sendRequest();
    void finish(bool ok, const uint16_t *regs, const char *error, bool closeConn);

  public:
    ModbusTcpClient();
    ~ModbusTcpClient();

    // Endpoint to use; a change closes the current connection
    void setEndpoint(const String &host, uint16_t port, uint8_t unit);

    // Start reading `count` (1..MODBUS_TCP_CLIENT_MAX_REGS) holding registers.
    // Returns false (without calling the callback) when a read is already in
    // flight or the arguments are invalid.
    bool read(uint16_t address, uint16_t count, ReadCallback callback);

    // Enforce the timeout of the read in flight; call regularly
    void poll();

    void close();

    bool isBusy() {
      return Step::Idle != _step;
    }

    bool isConnected() {
      return _connected;
    }
};

#endif // _MODBUS_TCP_CLIENT_H
