# Hexapod Protocol for Open Servo Controllers

**Disclaimer: MSB-set byte appended to GET reply**

An additional MSB-set byte has been added at the end of the GET reply (see commit `13dea99 Add MSB-set byte at the end of GET`). This trailing byte serves as a frame delimiter so the receiver can unambiguously detect the end of a GET reply and resynchronize after errors, since command bytes are the only bytes with the MSB set. This is a deviation from the original protocol description below and should be accounted for when parsing GET replies.

This document defines a simple serial protocol to send commands and receive feedback between the Chica Server app and any open servo controller board, such as an Arduino, RPI or the Servo2040 board.

## SET command

Sent by the phone to set the value of a pin or a sequence of pins, which can be a servo pulse width for a servo pin, or a digital or analog output for output pins such as the servo power relay.

Format:

```
[1 byte: S][1 byte: Start index][1 byte: Count][2 bytes: value][2 bytes: value]...[2 bytes: value]
```

Example #1: set pin 4 to 1500

```
[S][4][1][1500]
```

Example #2: set pins 7, 8, .. , 12 to 1007, 1008, … 1012

```
[S][7][6][1007][1008][1009][1010][1011][1012]
```

Note #1: the board and the phone agree on the pin indices based on the config of the app. For example if a particular pin is set to be the servo power relay, the board will interpret the value of 0 or 1 as a digital output on the pin, rather than the actual value.

Note #2: The optimal case is when all the servos are set to consecutive pins, and we can set all of them with a single command. But even if the pins are not consecutive, the phone can optimize the commands and set the servo values in batches of consecutive pins.

## GET command

Sent by the host to get the value of a pin or a sequence of pins, which can be a servo pulse width for a servo pin, or a sensor input such as voltage, current or the touch sensors.

Phone sends:

```
[1 byte: G][1 byte: Start index][1 byte: Count]
```

Servo controller replies:

```
[1 byte: G][1 byte: Start index][1 byte: Count][2 bytes: value][2 bytes: value]...[2 bytes: value]
```

Example #1: get the value on pin 4

```
[G][4][1]
```

Example #2: get the value on pins 20 to 25

```
[G][20][6]
```

## Protocol pin index map (host index → board hardware)

The host addresses a **flat logical pin index** (`0`–`26`), not a GPIO. The
firmware reassigns each index via `RP_hardwarePins_table` in
`src/chica-servo2040/main.h`. **The resolved value is not always a GPIO** — it may be
a servo channel, an ADC-mux channel address, or a real pin:

| Index | Name               | Description       | Resolves to               | Address / pin |Startup, SET RELAY 1 with no pose               │ servo_pose_staged false → continue; relay stays LOW, no motion ✓ │
├─────────────────────────────────────────────────┼──────────────────────────────────────────────────────────────────┤
│ Stage pose → SET RELAY 1                        │ load() latches staged pose; legs snap to it, no midpoint ✓       │
├─────────────────────────────────────────────────┼──────────────────────────────────────────────────────────────────┤
│ Over-current trip                               │ latch + disable + flag cleared ✓                                 │
├─────────────────────────────────────────────────┼──────────────────────────────────────────────────────────────────┤
│ Recovery SET RELAY 0 → SET RELAY 1 (no restage) │ flag false → enable ignored ✓                                    │
├─────────────────────────────────────────────────┼──────────────────────────────────────────────────────────────────┤
│ SET RELAY 0 → stage fresh pose → SET RELAY 1    │ drives fresh pose only, never the fault pose ✓                   │
├─────────────────────────────────────────────────┼──────────────────────────────────────────────────────────────────┤
│ GET during trip                                 │ servo writes still dropped → readb
| ----- | ------------------ | ----------------- | ------------------------- | :-----------------: |
| 0–17  | `SERVO1`–`SERVO18` | Servos            | PIO servo-cluster channel | channel `0`–`17` (GP0–GP17) |
| 18–23 | `TS1`–`TS6`        | Touch sensors     | ADC-mux channel           | `0b000`–`0b101` (0–5) |
| 24    | `CURR`             | Current in V/100  | ADC-mux channel           | `0b111` (7) |
| 25    | `VOLT`             | Voltage in A/100  | ADC-mux channel           | `0b110` (6) |
| 26    | `RELAY`            | Default relay pin | RP2040 GPIO               | GP26 (`A0_GPIO_PIN`) |
| 27    | `STATUS`           | Latched fault reg | firmware state (no pin)   |         —          |

Host indexes stop at `27`. Index `27` (`STATUS`) resolves to firmware state, not
a GPIO or mux channel — it is a read-only fault register (see below). GP28
(formerly `A2`) is **not used** by the firmware and **not addressable** over the
protocol.

Indices 18–25 are **mux channel addresses, not pins**: the 3-bit address drives
the select lines `ADC_ADDR_0/1/2` = GP22/GP24/GP25, routing one sensor onto the
shared ADC input `SHARED_ADC` = GP29.

The analog/digital split falls at the top of the map: **24/25 (`CURR`/`VOLT`) go
to the multiplexer**, and **26 (`RELAY`) is the only GPIO index**. The relay GPIO
(`A0`, default GP26, from `hexapod_config.cmake`) does not affect `CURR`/`VOLT`,
which are mux channel addresses.

## Telemetry units (voltage / current)

The bus current and voltage sensors are read via `GET` on the fixed indices
`CURR = 24` and `VOLT = 25` (these are consecutive, so one `GET [24][2]` returns
both). Their reply values are **not** raw ADC codes: the firmware has already
applied the on-board divider and shunt and reports fixed-point **centi-units** —

```
wire_count = round(engineering_value * 100)   // 0.01 A or 0.01 V per count
```

The values are unsigned and clamped to the 14-bit maximum (16383), i.e. a full
scale of 163.83 A / 163.83 V, so a fault-current spike saturates rather than
wrapping. The host recovers engineering units with a single multiply by the
reciprocal `0.01` and carries **no** other scaling. This factor is
protocol-defined and must match on both ends (firmware `TELEMETRY_COUNTS_PER_UNIT`,
host `kVoltsPerCount`/`kAmpsPerCount`).

Worked example — read both, current `1.50 A`, voltage `8.30 V`:

```
Request:  C7 18 02                     GET(start=24, count=2)
Reply:    C7 18 02 16 01 3E 06 F2
          └cmd  │  │  └─── └─── └── checksum (XOR of prior bytes) | 0x80
          start=24 (0x18), count=2 (0x02)
          current = 0x16 | (0x01<<7) = 150  →  150 * 0.01 = 1.50 A   (values[0])
          voltage = 0x3E | (0x06<<7) = 830  →  830 * 0.01 = 8.30 V   (values[1])
```

Ordering is current-then-voltage because `CURR` (24) precedes `VOLT` (25) on the
wire. The relay is addressed by `SET` on index `RELAY = 26`, whose GPIO is chosen
entirely on the board — the host never configures a relay pin.

Note: touch-sensor `GET`s (indices 18..23) still reply with a raw ADC-derived code
(a different, ~10-bit scaling) and are not consumed by the host.

## Fault / status register (`STATUS = 27`)

The firmware runs an autonomous over-current protection: if the rail
draws too much for too long it **latches a fault**, disables the servo cluster,
drops the relay, and shows a red LED — all without host involvement. Because the
protocol is strictly polled (the board only ever replies to a `GET`; `SET` is
silent), the host learns about this by polling the read-only `STATUS` register at
index `27`. `STATUS` reflects firmware state, has no pin, and is **read-only**:
any `SET` overlapping index 27 is rejected (as is a range that straddles it, e.g.
`SET [26][2]` — write `RELAY` alone with `SET [26][1]`).

`GET [27][1]` returns a 14-bit fault word, LSB-first. **`0` means clean** (no
fault); non-zero means a fault is latched:

| Bits  | Field          | Meaning                                                         |
| ----- | -------------- | --------------------------------------------------------------- |
| 0     | `TRIPPED`      | 1 = over-current latch active (definitive flag)                 |
| 1–10  | `TRIP_CURRENT` | current at the trip, `0.1 A`/count, 0–102.3 A (saturates)       |
| 11–13 | —              | reserved (0)                                                    |

Decode:

```
tripped   =  value        & 0x1
trip_amps = ((value >> 1)  & 0x3FF) * 0.1
```

The word is **sticky**: it keeps reporting the trip on every poll — no matter how
slowly the host polls — until the host clears it by sending `SET RELAY 0` (the
same disable line). That clears the latch and re-arms; a subsequent `SET RELAY 1`
re-enables (subject to the staged-pose requirement below) and `STATUS` reads `0`
again. Live current reads ~0 once the relay is open, which is why the current *at
the trip* is captured in the word rather than read back afterwards.

Recommended host handling: on a non-zero `STATUS`, stop issuing servo `SET`s,
surface the fault (trip current) to the operator, then recover with
`SET RELAY 0` → stage a fresh pose (servo `SET`s) → `SET RELAY 1`. The board does
**not** resume the pre-trip positions — the pose that drew the fault current is
never re-applied automatically (see below).

### Enabling servos: the staged-pose requirement

The board **never drives a servo to a position the host has not commanded** —
there is no power-on "center to midpoint" behaviour. The first position a servo
assumes is always one the host has sent. This applies identically at cold startup
and after an over-current recovery.

Mechanically, a servo `SET` sent while the relay is **off** (disabled) is
*staged*: the pulse is buffered in the PWM registers but not driven. `SET RELAY 1`
then latches whatever is staged. Consequences for the host:

- **`SET RELAY 1` is ignored** (the relay stays open, no servos move) **until at
  least one servo position has been staged since the last disable.** An enable
  with nothing staged is a silent no-op — the board is polled, so the host
  confirms success by observing servo response, not an ack.
- A **disable resets the requirement**: both `SET RELAY 0` and an over-current
  trip clear the staged-pose flag, so a fresh pose must be staged before the next
  enable. This is why recovery is `SET RELAY 0` → stage pose → `SET RELAY 1`.
- Only staged servos are driven on enable; any servo left un-commanded stays limp
  (no pulse), never a midpoint.

Recommended bring-up (startup **and** recovery):

```
SET <servos> <pose>      # stage a full pose while the relay is off
SET RELAY 1              # latches the staged pose atomically; legs snap to it
... stream SET <servos> ...   # subsequent SETs drive immediately
```

### Reading telemetry and status in one frame

`STATUS` (27) sits one past the telemetry block, so a single `GET [24][4]` reads
`CURR`, `VOLT`, `RELAY`, `STATUS` together. `RELAY` (26) has no sensor and reads
back a defined `0`.

```
Request:  C7 18 04                             GET(start=24, count=4)
Reply:    C7 18 04 16 01 3E 06 00 00 00 00 XX
          └cmd  │  │  └CURR └VOLT └RELAY └STATUS └ checksum (XOR prior) | 0x80
          start=24 (0x18), count=4 (0x04)
          current = 0x16 | (0x01<<7) = 150  →  1.50 A   (values[0])
          voltage = 0x3E | (0x06<<7) = 830  →  8.30 V   (values[1])
          relay   = 0x00 | (0x00<<7) = 0                (values[2], always 0)
          status  = 0x00 | (0x00<<7) = 0    →  no fault (values[3])
```

## Command bytes vs. Value bytes

All command bytes (`S` and `G`) have their most significant bit set to 1, and all other bytes have their most significant bit cleared. This is very useful to recover from errors. As a result, only 7 bits are available for commands (total of 128 command types), and 14 bits for the data values. And we can have a total of 128 pins.

Examples:

```
S command byte: 0x53 | 0x80
G command byte: 0x47 | 0x80
```

Writing a 14 bit value to the buffer:

```c
int value;
buffer[0] = value & 0x7F;
buffer[1] = (value >> 7) & 0x7F;
```

Reading a 14 bit value from the buffer:

```c
int value = (buffer[0] & 0x7F) | ((buffer[1] & 0x7F) << 7);
```
