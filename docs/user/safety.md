# Safety

![Safety settings](screenshots/settings-safety-dark-desktop.png)

## Protection checks

The OpenEVSE controller continuously runs hardware safety checks — diode
check, GFCI self-test, ground monitoring, stuck-relay detection, vent-required
detection, and temperature monitoring. A failed check faults the charger
immediately (red ring on the [Dashboard](dashboard.md), entry in
[History](history.md), counters on
[Monitoring → Safety](monitoring.md)).

The individual checks are switched on and off under
[Installer tools](installer-tools.md) (`/settings/installer`), behind the
installer password — not on this page. Leave them on: a charger with a
disabled safety check should not be left in service. The one exception is the
GFCI self-test, which is correctly off when the circuit is itself protected by
a GFCI. The error claim outranks everything else in the
[priority system](../developer/architecture.md#evsemanager-and-the-clientpriority-system):
nothing can command a faulted charger to charge.

## Temperature throttling

When the EVSE temperature exceeds the throttle setpoint the charge current is
reduced automatically, and charging shuts down entirely at the over-temperature
shutdown threshold. Both thresholds are configurable on the Safety page.

## Low voltage throttling

With a live voltage measurement — a [Modbus TCP meter](integrations.md#modbus-tcp-meter-chint-ddsu666), the MQTT voltage topic or [Shelly LNM](integrations.md#shelly-lnm) — the charge current can be lowered automatically when the supply voltage sags (house loads, long cable runs, a weak grid). Configure it on the Safety page.

| Option | Default | Description |
|--------|---------|-------------|
| `voltage_throttle_enabled` | `false` | Lower the charge current on low voltage |
| `voltage_throttle_low` | `212` | V. When the mean voltage over an interval is below this, the current limit goes down one step |
| `voltage_throttle_high` | `218` | V. When every reading of an interval is above this, the limit goes up one step |
| `voltage_throttle_interval` | `30` | Seconds per decision (10 to 600). The voltage is sampled every 5 seconds within it |
| `voltage_throttle_step` | `1` | A per step (1 to 10) |

Between the two thresholds the limit is kept, so the charger does not hunt. The first step down starts from what the vehicle actually draws, so it takes effect even when the car limits its own current below the pilot. The limit never goes below the charger's minimum current and never raises what another feature (Load Shaper, Solar divert, a schedule, ...) asked for. It is removed when it reaches the maximum current, when the vehicle stays well below it, when charging stops, or when no live voltage is available.

On the TFT display the pilot line names what sets the current: `low voltage` while this throttle holds it down, `grid limit` for the Load Shaper.

`/status` and the events report `voltage_throttle_active`, `voltage_throttle_current` (A, 0 when not throttling) and `voltage_throttle_voltage` (mean of the last interval).

## Boot lock & heartbeat

- **Boot lock** keeps the charger disabled after power-up until the gateway is
  fully online — protecting against a stale controller state after outages.
- The **heartbeat** watchdog reverts the charger to a safe fallback current if
  the WiFi gateway stops responding to the controller (interval and fallback
  current configurable).
