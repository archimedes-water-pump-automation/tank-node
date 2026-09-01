# tank-node

ESP32-C3 firmware that measures water level in a tank with an ultrasonic
sensor and publishes it over MQTT.

Deliberately dumb: it reads, it publishes, it reports failure honestly. All
pump logic lives in [`pump-controller`](../pump-controller).

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

Every `LEVEL_PUBLISH_MS` (5 s), the node takes up to 5 ultrasonic readings,
publishes the median, and sleeps the remainder of the interval.

- **Median filter.** Readings off a moving water surface are noisy, and a
  single bad sample must not move the pump.
- **Range gate.** Values outside `DIST_MIN_VALID_CM`–`DIST_MAX_VALID_CM` are
  rejected. The lower bound clears the transducer's blind zone.
- **Failure is published, not hidden.** Fewer than 3 good pings publishes
  `valid:false`. Silence and a bad reading must not look the same downstream.
- **Last will.** If the node drops off uncleanly the broker publishes
  `valid:false`, so the controller reacts in one cycle rather than waiting out
  its 20 s stale timer.

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

Shared with `pump-controller`. Changing either side requires changing both.

**Publishes** to `watertank/tank-01/level` — QoS 0, **not retained**:

```json
{"event":"level","device":"tank-01","distance_cm":62.5,
 "valid":true,"uptime_s":360}
```

Sensor unreadable:

```json
{"event":"level","device":"tank-01","distance_cm":null,
 "valid":false,"reason":"sensor_unreadable","uptime_s":360}
```

`distance_cm` is measured from the sensor face downward and *decreases* as the
tank fills.

Not retained on purpose. A retained level reading is by definition old, but it
arrives the instant a subscriber connects, so arrival-time freshness would
score it as current.

## Configuration

`main/config.h`:

| Constant | Default | Notes |
|---|---|---|
| `LEVEL_PUBLISH_MS` | 5000 | Controller's `LEVEL_STALE_MS` must be a multiple |
| `DIST_MIN_VALID_CM` | 3.0 | Below the transducer blind zone |
| `DIST_MAX_VALID_CM` | 400.0 | Sensor range limit |

This stream is a control input, not just telemetry, which is why it runs far
faster than a reporting interval would need. Slowing it down without widening
`LEVEL_STALE_MS` on the controller will cause spurious faults.

## Known gaps

- Credentials are compiled in. Move to NVS before deployment.
- Plaintext MQTT. Switch to `mqtts://` with a CA certificate.
- No OTA update path.
- No local buffering — readings taken while offline are lost, which is correct
  for a freshness-critical stream but worth knowing.
