# Load shaper

The current shaper caps total household power draw: it watches your live house
load and reduces the charge current so the total stays under the maximum your
supply (or tariff) allows — no more tripped mains when the oven, heat pump,
and car coincide.

![Load shaper settings](screenshots/settings-shaper-dark-desktop.png)

- Set **Max power allowed** to your supply/tariff limit (watts).
- Feed it live household power via **MQTT** (*Live power load MQTT topic*) or
  by POSTing `{"shaper_live_pwr": <watts>}` to the device's `/status`
  endpoint periodically.
- When enabled, the shaper takes a high-priority claim on the charger; it can
  be temporarily disabled over HTTP (`POST /shaper` with `shaper=<value>`) or
  MQTT.
- Smoothing, a minimum pause time, and a data-timeout interval are
  configurable — if the live feed stops arriving, the shaper falls back to a
  safe current.
- `current_shaper_release_on_timeout` (default `false`) changes what happens
  when the live feed stops for `current_shaper_data_maxinterval` seconds:
  instead of pausing the charge, the shaper releases its claim and the charger
  carries on as if the shaper were off, until the next live power update
  starts shaping again. Choose this only when an unprotected charge is
  preferable to a paused one: while the feed is down nothing limits the house
  total. It does not apply while a load sharing limit is active.

The [Dashboard](dashboard.md) shows the live house load, the smoothed
available current, and the resulting charge rate whenever the shaper is
active.

## Load sharing between chargers (Labs preview)

Where the shaper caps one charger against the whole house, **load sharing**
splits one supply between several OpenEVSE units: a *controller* keeps the sum
of the group's charge currents under a site limit and hands each *member* an
allocation.

This is a **Labs preview**: the firmware support is in, the feature is still
settling. The page lives at **Settings → Load Sharing**
(`/settings/loadsharing`) and is hidden until you turn on *Enable Labs
features* under [Settings → Developer Tools](settings.md) → OpenEVSE Labs. Two
things to know before you turn it on:

- Every station in the group must run the same firmware release.
- Stations cannot yet authenticate to each other: if you have set a web
  password (`www_password`), peers will fail to connect. Leave the password
  off on load-sharing stations for now, or keep them on a trusted network.

The page covers:

- **Settings** — enable, group ID, role (controller or member), site max
  current, safety factor, and the controller host for members.
- **Failsafe** — heartbeat timeout plus what happens when the group goes
  quiet: drop to a safe current, or stop charging.
- **Peer management** — discover units on the network, add or remove them by
  host/IP, set per-peer priority, and see each peer's state, allocation, and
  the reason it was given.

Members surface their assigned limit on the [Dashboard](dashboard.md) alongside
the shaper, since both arrive as claims on the charge current. For the
controller/member setup itself, see [Load sharing](load-sharing.md).
