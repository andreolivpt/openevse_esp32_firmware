# Integrations — MQTT, Home Assistant, EmonCMS

## MQTT

MQTT is the primary integration: the charger publishes its full status and
accepts control commands over a broker of your choice (`mqtt://` or `mqtts://`).

![MQTT settings](screenshots/settings-mqtt-dark-desktop.png)

Configure the broker address, credentials, and base topic under
Settings → MQTT. The full topic reference is in the
[MQTT API documentation](../mqtt.md) and the
[MQTT developer guide](../Developers_Guide_MQTT.md); the common ones:

- Status is published under `<base-topic>/…` (state, power, session energy,
  temperatures, …) — the announce topic makes devices discoverable.
- Control: `<base-topic>/override/set`, `<base-topic>/charge_rate/set`,
  `<base-topic>/divertmode/set`, and friends.
- Data feeds *into* the charger — solar/grid for
  [divert](solar-divert.md), house load for the [shaper](load-shaper.md),
  vehicle SOC/range for the [dashboard](dashboard.md) — are plain topics you
  point at your existing sensors.

### Home Assistant

The [OpenEVSE integration](https://www.home-assistant.io/integrations/openevse/)
(and the community HACS integration) ride on this MQTT API: point the charger
at the same broker as Home Assistant and entities appear for state, power,
energy, and control. Vehicle SOC from a Home Assistant-connected car can be
fed back to the charger via the `mqtt_vehicle_soc` / `mqtt_vehicle_range`
topics — see [Vehicle](vehicle.md).

## EmonCMS

The charger can post its metrics every 30 s to [emoncms.org](https://emoncms.org)
or a self-hosted EmonCMS/emonPi (HTTP or HTTPS) — long-term, full-resolution
energy logging and dashboards.

![EmonCMS settings](screenshots/settings-emoncms-dark-desktop.png)

## Shelly LNM

Shelly Local Network Messaging (LNM) can provide the live grid power and voltage measurements used by OpenEVSE. Configure it from **Settings → Connectivity → Shelly LNM** (`/settings/shellylnm`).

| Option | Default | Description |
|--------|---------|-------------|
| `shelly_lnm_enabled` | `false` | Listen to Shelly LNM messages |
| `shelly_lnm_addr` | `239.255.55.55` | Multicast address, same as configured on the Shelly device |
| `shelly_lnm_port` | `5555` | UDP port, same as configured on the Shelly device |
| `shelly_lnm_device` | empty | Only accept messages from this device ID (e.g. `shellypro3em-8813bfe1ab68`). Empty accepts any device |
| `shelly_lnm_power_field` | `act_power` | JSON field holding the grid power: `act_power` for a Shelly EM / EM Pro / EM Mini, or `total_act_power`, `a_act_power`, `b_act_power`, `c_act_power` for a 3EM (total or per phase) |
| `shelly_lnm_voltage_field` | `voltage` | JSON field holding the voltage: `voltage` for EM / EM Pro / EM Mini, or `a_voltage`, `b_voltage`, `c_voltage` for a 3EM phase |

While enabled, these measurements replace the MQTT inputs for:

* the grid voltage (`mqtt_vrms`);
* the Solar divert grid excess (`mqtt_grid_ie`, grid mode only);
* the Load Shaper live power, unless a dedicated `mqtt_live_pwr` topic (different from `mqtt_grid_ie`) is configured, in which case that MQTT topic keeps feeding the shaper.

The replaced MQTT topics are not subscribed to. Values are applied at most every 5 seconds, and voltage changes below 1 V are ignored. If the Shelly stops sending, the last value is kept (as with MQTT); the Load Shaper has its own data timeout. The listener state, data age, power and voltage are shown live in the Shelly LNM settings.

**Security:** LNM messages are unauthenticated UDP multicast. Anyone on the local network can send packets that set the grid power and voltage seen by OpenEVSE, which is comparable to an unauthenticated MQTT broker. Use `shelly_lnm_device` to ignore other devices' messages, and only enable this on a trusted network.

## Modbus TCP meter (CHINT DDSU666)

A single phase CHINT DDSU666 meter on the charger circuit, reached through a Modbus TCP gateway (RS485 to Ethernet / WiFi), can provide the voltage and charging current. Configure it from **Settings → Connectivity → Modbus TCP meter** (`/settings/modbusmeter`), which also shows the connection state and the live readings, or through the `/config` API, for example:

```bash
curl -X POST http://openevse.local/config -H 'Content-Type: application/json' \
  -d '{"modbus_meter_enabled": true, "modbus_meter_host": "192.168.0.50", "modbus_meter_unit": 1}'
```

| Option | Default | Description |
|--------|---------|-------------|
| `modbus_meter_enabled` | `false` | Read the meter |
| `modbus_meter_host` | empty | IP address or host name of the Modbus TCP gateway |
| `modbus_meter_port` | `502` | TCP port of the gateway |
| `modbus_meter_unit` | `1` | Modbus address (unit id) of the meter on the RS485 bus |
| `modbus_meter_interval` | `5` | Seconds between readings (1 to 30) |
| `modbus_meter_timeout` | `90` | Seconds without a valid reading before falling back to the default behaviour (10 to 120, and never less than two polls) |

The gateway must speak Modbus TCP (MBAP framing); gateways in transparent "RTU over TCP" mode are reported as `invalid response`. The meter is read with function 0x03: voltage, current and active power from 0x2000, imported energy from 0x4000.

While readings are fresh:

* the voltage replaces the MQTT voltage input (`mqtt_vrms` is not subscribed to); changes below 1 V are ignored;
* the meter current replaces the OpenEVSE current sensor reading while charging, so the reported power follows the meter;
* the session / total (and daily, weekly, ...) energy counters add what the meter's import register counted while charging, instead of integrating voltage × current. A missed energy read, a register that goes backwards (meter reset or replaced) or an implausible jump falls back to voltage × current for that step and resyncs on the next good value. Energy counted by the meter while not charging (standby) is never added.

When no valid reading arrives for `modbus_meter_timeout` seconds, or the option is disabled, the configured voltage and the OpenEVSE current sensor are used again. Do not enable this together with Shelly LNM, both would feed the voltage.

`/status` and the WebSocket / MQTT events report `modbus_meter_connected`, `modbus_meter_active` (meter values in use), `modbus_meter_data_age` (ms), `modbus_meter_voltage`, `modbus_meter_current`, `modbus_meter_power` (W), `modbus_meter_energy` (imported kWh, as counted by the meter), `modbus_meter_errors` and, after a failure, `modbus_meter_last_error`.

## Modbus TCP grid power (Load Shaper)

The [Load Shaper](load-shaper.md) can read the whole-house grid power straight from a Modbus TCP device instead of an MQTT topic. The defaults match an AlphaESS inverter (unit 85, holding register `0x0021`, signed 32 bit watts, positive when importing). AlphaESS inverters accept a single Modbus TCP client, so when something else also reads it, put a Modbus TCP proxy in front and point both at the proxy. Configure it from **Settings → Connectivity → Modbus TCP meter** (`/settings/modbusmeter`), section *Grid power*.

| Option | Default | Description |
|--------|---------|-------------|
| `modbus_grid_enabled` | `false` | Feed the Load Shaper from this device |
| `modbus_grid_host` | empty | IP address or host name of the device (or proxy) |
| `modbus_grid_port` | `502` | TCP port |
| `modbus_grid_unit` | `85` | Modbus address (unit id) |
| `modbus_grid_register` | `33` (`0x0021`) | Holding register with the grid power |
| `modbus_grid_format` | `int32` | `int16`, `uint16`, `int32`, `uint32` or `float32`; 32 bit values high word first |
| `modbus_grid_scale` | `1.0` | Multiplier to get watts (e.g. `1000` for a value in kW) |
| `modbus_grid_invert` | `false` | Invert the sign, for devices that report export as positive |
| `modbus_grid_interval` | `5` | Seconds between readings (1 to 60) |

Each reading becomes the shaper live power, exactly like the `mqtt_live_pwr` topic, which is not subscribed to while this source is enabled. Failed reads send nothing; the shaper's data timeout (`current_shaper_data_maxinterval`, and `current_shaper_release_on_timeout`) decides what happens when readings stop. Do not enable it together with Shelly LNM, both would feed the shaper.

`/status` reports `modbus_grid_connected`, `modbus_grid_data_age` (ms), `modbus_grid_power` (W), `modbus_grid_errors` and, after a failure, `modbus_grid_last_error`.

## Plain HTTP

Everything MQTT can feed in, HTTP can too: POST JSON to the device's `/status`
endpoint with any of `voltage`, `shaper_live_pwr`, `solar`, `grid_ie`,
`battery_level`, `battery_range`, `time_to_full_charge`. The complete HTTP API
is documented at
[openevse.stoplight.io](https://openevse.stoplight.io/docs/openevse-wifi-v4/).
