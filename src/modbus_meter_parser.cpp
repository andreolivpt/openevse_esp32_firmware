// Minimal Modbus TCP framing helpers, see modbus_meter_parser.h

#include <string.h>

#include "modbus_meter_parser.h"

#define MODBUS_FC_READ_HOLDING_REGISTERS 0x03
#define MODBUS_MBAP_HEADER_LEN 7 // transaction(2) protocol(2) length(2) unit(1)

static inline uint16_t be16(const uint8_t *p)
{
  return (uint16_t)((p[0] << 8) | p[1]);
}

size_t modbus_tcp_build_read(uint8_t *buf, size_t buf_len,
                             uint16_t transaction_id, uint8_t unit_id,
                             uint16_t address, uint16_t count)
{
  if(nullptr == buf || buf_len < MODBUS_TCP_READ_REQUEST_LEN ||
     0 == count || count > MODBUS_TCP_MAX_READ_REGS)
  {
    return 0;
  }

  buf[0] = transaction_id >> 8;
  buf[1] = transaction_id & 0xff;
  buf[2] = 0; // protocol id, always 0 for Modbus
  buf[3] = 0;
  buf[4] = 0; // length of what follows: unit + PDU (5 bytes)
  buf[5] = 6;
  buf[6] = unit_id;
  buf[7] = MODBUS_FC_READ_HOLDING_REGISTERS;
  buf[8] = address >> 8;
  buf[9] = address & 0xff;
  buf[10] = count >> 8;
  buf[11] = count & 0xff;

  return MODBUS_TCP_READ_REQUEST_LEN;
}

ModbusParseResult modbus_tcp_parse_read(const uint8_t *buf, size_t len,
                                        uint16_t transaction_id, uint16_t count,
                                        uint16_t *regs, size_t &consumed,
                                        uint8_t &exception_code)
{
  consumed = 0;

  if(len < MODBUS_MBAP_HEADER_LEN) {
    return ModbusParseResult::NeedMore;
  }

  uint16_t tid = be16(buf);
  uint16_t protocol = be16(buf + 2);
  uint16_t length = be16(buf + 4); // unit id + PDU

  // A Modbus TCP ADU is at most 260 bytes, the smallest response is an
  // exception (unit + fc + code)
  if(0 != protocol || length < 3 || length > 254) {
    return ModbusParseResult::BadFrame;
  }

  size_t frame_len = 6 + (size_t)length;
  if(len < frame_len) {
    return ModbusParseResult::NeedMore;
  }

  // From here on a whole frame is available
  consumed = frame_len;

  if(tid != transaction_id) {
    return ModbusParseResult::WrongTransaction;
  }

  // The unit id is not checked: some gateways rewrite it, and there is only
  // ever one outstanding request per connection.
  uint8_t fc = buf[7];
  if((MODBUS_FC_READ_HOLDING_REGISTERS | 0x80) == fc) {
    exception_code = buf[8];
    return ModbusParseResult::Exception;
  }

  size_t data_len = (size_t)count * 2;
  if(MODBUS_FC_READ_HOLDING_REGISTERS != fc ||
     length != 3 + data_len ||
     buf[8] != data_len)
  {
    return ModbusParseResult::BadFrame;
  }

  const uint8_t *data = buf + 9;
  for(uint16_t i = 0; i < count; i++) {
    regs[i] = be16(data + i * 2);
  }

  return ModbusParseResult::Ok;
}

bool modbus_meter_energy_delta(double last_kwh, double now_kwh, uint32_t elapsed_ms, double &wh)
{
  // NaN compares false everywhere, reject it explicitly
  if(last_kwh != last_kwh || now_kwh != now_kwh) {
    return false;
  }

  double delta = (now_kwh - last_kwh) * 1000.0;
  if(delta < 0) {
    return false;
  }

  uint32_t window = elapsed_ms < 60000 ? 60000 : elapsed_ms;
  double limit = 30000.0 * window / 3600000.0;
  if(delta > limit) {
    return false;
  }

  wh = delta;
  return true;
}

float modbus_regs_to_float(const uint16_t *regs)
{
  uint32_t raw = ((uint32_t)regs[0] << 16) | regs[1];
  float value;
  memcpy(&value, &raw, sizeof(value));
  return value;
}
