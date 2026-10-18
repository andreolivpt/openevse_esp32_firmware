#ifndef _MODBUS_METER_PARSER_H
#define _MODBUS_METER_PARSER_H

// Minimal Modbus TCP framing helpers for reading holding registers (FC 0x03).
//
// Kept free of Arduino / network dependencies so it can be unit tested on the
// host (see test/test_modbus_meter).

#include <stddef.h>
#include <stdint.h>

#define MODBUS_TCP_READ_REQUEST_LEN 12   // MBAP header (7) + FC + addr + count
#define MODBUS_TCP_MAX_READ_REGS    125  // protocol limit for FC 0x03

enum class ModbusParseResult {
  Ok,               // a complete, valid response for the expected transaction
  NeedMore,         // not enough bytes yet, keep reading
  WrongTransaction, // complete frame for another transaction id, discard it
  Exception,        // the device answered with a Modbus exception
  BadFrame          // invalid / unexpected data, the stream should be reset
};

// Build a "read holding registers" request. Returns the request length
// (MODBUS_TCP_READ_REQUEST_LEN) or 0 if the buffer is too small or the
// register count is out of range.
size_t modbus_tcp_build_read(uint8_t *buf, size_t buf_len,
                             uint16_t transaction_id, uint8_t unit_id,
                             uint16_t address, uint16_t count);

// Parse a response to a request built by modbus_tcp_build_read().
//
// On Ok, `count` registers are written to `regs`. `consumed` is set to the
// number of bytes making up the frame whenever a whole frame was recognised
// (Ok, WrongTransaction, Exception), so the caller can drop them from its
// receive buffer. `exception_code` is set on Exception.
ModbusParseResult modbus_tcp_parse_read(const uint8_t *buf, size_t len,
                                        uint16_t transaction_id, uint16_t count,
                                        uint16_t *regs, size_t &consumed,
                                        uint8_t &exception_code);

// Decode an IEEE 754 float stored in two registers, high word first (the
// layout used by CHINT DDSU666 / DTSU666 meters).
float modbus_regs_to_float(const uint16_t *regs);

// Energy added between two readings of a meter's cumulative import register.
// Returns false (and leaves `wh` untouched) when the step cannot be trusted:
// the register went backwards (meter reset / replaced) or jumped further than
// a 30 kW load could explain over max(elapsed_ms, 60 s).
bool modbus_meter_energy_delta(double last_kwh, double now_kwh, uint32_t elapsed_ms, double &wh);

// Register value formats, 32 bit values high word first
enum class ModbusValueFormat : uint8_t {
  Int16,
  UInt16,
  Int32,
  UInt32,
  Float32
};

// "int16", "uint16", "int32", "uint32" or "float32" (case sensitive).
// Returns false and leaves `format` untouched for anything else.
bool modbus_value_format_parse(const char *name, ModbusValueFormat &format);

// Number of registers a value of this format takes (1 or 2)
uint16_t modbus_value_format_regs(ModbusValueFormat format);

// Decode one value from `regs` (modbus_value_format_regs() registers)
double modbus_decode_value(const uint16_t *regs, ModbusValueFormat format);

#endif // _MODBUS_METER_PARSER_H
