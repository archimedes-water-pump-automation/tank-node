# tank-node

ESP32-C3 firmware that measures water level in a tank with an ultrasonic
sensor and publishes it over MQTT.

It reads, it publishes, it reports failure honestly. It also owns the one
decision that depends on the reading: whether the tank is full. The pump
controller receives that decision on its own topic and never sees a distance,
so the thresholds behind it exist in exactly one place — here, beside the
sensor. Everything about *the pump* still lives in
[`pump-ctl`](https://github.com/archimedes-water-pump-automation/pump-ctl).

## Why this is a separate board

The ultrasonic sensor sits at the tank, far from the pump controller. ECHO is
not a data line — it is a microsecond-timed pulse width — so any noise picked
up over a long cable becomes a plausible-looking distance rather than an
obvious error. Worse, a mains pump motor is a strong EMI source, so the
interference would arrive precisely when the pump is running.

Putting a microcontroller at the sensor removes the cable problem entirely.
The cost is that a safety-critical input now travels over a network, which is
why the controller enforces a staleness timeout.

## Behaviour

Every `LEVEL_PUBLISH_MS` (5 s), the node takes up to 5 ultrasonic readings and
publishes the median twice — as a distance to `archimedes-server`, and as the
state derived from it to `pump-ctl` — then sleeps the remainder of the
interval.

- **Two topics, two audiences.** The server stores measurements; the
  controller acts on decisions. Sending the raw distance to the controller as
  well would let it apply a second copy of the thresholds below, free to drift
  from these, with no way to tell which copy had drifted.

- **Median filter.** Readings off a moving water surface are noisy, and a
  single bad sample must not move the pump.
- **Range gate.** Values outside `DIST_MIN_VALID_CM`–`DIST_MAX_VALID_CM` are
  rejected. The lower bound clears the transducer's blind zone.
- **Hysteresis.** The tank is `full` at `DIST_FULL_CM` and only `refillable`
  again at the further `DIST_REFILL_CM`; between them it is `partial`, where a
  running pump keeps running and an idle one stays idle. One threshold instead
  of two would make the pump relay chatter as the water surface moves across
  it.
- **Failure is published, not hidden.** Fewer than 3 good pings publishes
  `valid:false` on the level topic and `state:"unknown"` on the full_tank
  topic. Silence and a bad reading must not look the same downstream.
- **Last will.** If the node drops off uncleanly the broker publishes
  `state:"unknown"` on the full_tank topic, so the controller reacts in one
  cycle rather than waiting out its 20 s stale timer. MQTT allows one will per
  connection and this is the one worth having: that stream holds a pump off,
  while a level that stops arriving only means a volume stops updating.

## Hardware

| Signal | GPIO | Notes |
|---|---|---|
| TRIG | 3 | Direct. 3.3 V clears the sensor's logic-high threshold |
| ECHO | 4 | 1 kΩ / 2 kΩ divider, placed at the C3 end |

Use a **JSN-SR04T**, not a bare HC-SR04 — the waterproof transducer head
survives a humid tank, and it is a drop-in protocol match.

The divider goes at the C3 end so the cable carries a full 5 V swing and the
resistors sit at the pin. Low values are deliberate: high-value resistors plus
pin capacitance slow the echo edges enough to skew distance readings.

Power the board locally from its own 5 V supply. Do not run 5 V over from the
pump controller — a long supply wire reintroduces the ground offset that
splitting the boards was meant to remove. Fit 100 µF and 0.1 µF across the
rails near the sensor; the 40 kHz transmit burst draws current spikes, and
skipping this shows up as intermittent bad readings rather than a clean
failure.

Mount the transducer **above** the maximum water line. Distance zero is not
measurable — it means the transducer is submerged and dead.

## Build

```sh
cp main/secrets.h.example main/secrets.h   # then edit it
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Requires ESP-IDF v5.x.

## MQTT contract

Defined in [MQTT_CONTRACT.md](MQTT_CONTRACT.md), which is mirrored in every
repository of this system. Each topic has exactly one consumer, so changing a
field means changing it in two places.

**Publishes** to `watertank/tank-01/level` — QoS 0, **not retained**, read by
`archimedes-server`:

```json
{"event":"level","device":"tank-01","timestamp":"2026-09-05T03:10:12Z",
 "valid":true,"distance_cm":62.5,"uptime_s":360}
```

Sensor unreadable:

```json
{"event":"level","device":"tank-01","timestamp":"2026-09-05T03:10:12Z",
 "valid":false,"distance_cm":null,"reason":"sensor_unreadable","uptime_s":360}
```

`distance_cm` is measured from the sensor face downward and *decreases* as the
tank fills. It is `null` whenever `valid` is false — never `0`, which would
read downstream as a tank filled to the sensor.

**Publishes** to `watertank/tank-01/full_tank` — QoS 0, **not retained**, read
by `pump-ctl`, derived from the same reading:

```json
{"event":"full_tank","device":"tank-01","timestamp":"2026-09-05T03:10:12Z",
 "state":"refillable","uptime_s":360}
```

| `state` | Means | What the controller does |
|---|---|---|
| `full` | `distance_cm <= DIST_FULL_CM` | Stops the pump, releases the supply valve |
| `partial` | Between the thresholds | Nothing: running stays running, idle stays idle |
| `refillable` | `distance_cm >= DIST_REFILL_CM` | May start, once inflow is confirmed |
| `unknown` | Sensor unreadable, or this node is gone | Faults, pump held off |

No distance appears on this topic. The controller is told what the tank *is*,
not what it measures.

Last will, carrying neither `timestamp` nor `uptime_s` because the broker
publishes it on this node's behalf long after the node wrote it:

```json
{"event":"full_tank","device":"tank-01","state":"unknown",
 "reason":"node_offline"}
```

Both streams are published every cycle rather than only on a change: the
controller faults when the full_tank stream goes stale, so it is the silence
that has to be detectable, not just the transition. Neither is retained — a
retained reading is by definition old, but it arrives the instant a subscriber
connects, so arrival-time freshness would score it as current.

`timestamp` is UTC, and is present only once SNTP has landed. The board has no
battery-backed RTC, so stamping every event before that would put 1970 in the
server's database; the field is omitted instead and the consumer falls back to
its own receipt time. The clock never gates a reading or a publish.

## Configuration

`main/config.h`:

| Constant | Default | Notes |
|---|---|---|
| `LEVEL_PUBLISH_MS` | 5000 | Controller's `TANK_STALE_MS` must be a multiple |
| `DIST_MIN_VALID_CM` | 3.0 | Below the transducer blind zone |
| `DIST_MAX_VALID_CM` | 400.0 | Sensor range limit |
| `DIST_FULL_CM` | 12.0 | At or below this the tank is `full`. Must clear the transducer blind zone |
| `DIST_REFILL_CM` | 35.0 | At or above this the tank is `refillable` |
| `SNTP_SERVER` | pool.ntp.org | Source of the UTC `timestamp` field |

`DIST_FULL_CM` and `DIST_REFILL_CM` depend on where the transducer is
physically mounted. Measure from the sensor face to the intended stop level
and add margin. They used to live in the pump controller's config, applied to
a distance it received; the controller no longer sees a distance, so they live
here, and changing them changes the system's behaviour from one place.

The full_tank stream is a control input, not just telemetry, which is why one
reading every 5 s is far faster than a reporting interval would need. Slowing
it down without widening `TANK_STALE_MS` on the controller will cause spurious
faults.

## Known gaps

- Credentials are compiled in. Move to NVS before deployment.
- Plaintext MQTT. Switch to `mqtts://` with a CA certificate.
- No OTA update path.
- No local buffering — readings taken while offline are lost, which is correct
  for a freshness-critical stream but worth knowing.
