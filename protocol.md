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

| Index | Name               | Description       | Resolves to               | Address / pin |
| ----- | ------------------ | ----------------- | ------------------------- | :-----------------: |
| 0–17  | `SERVO1`–`SERVO18` | Servos            | PIO servo-cluster channel | channel `0`–`17` (GP0–GP17) |
| 18–23 | `TS1`–`TS6`        | Touch sensors     | ADC-mux channel           | `0b000`–`0b101` (0–5) |
| 24    | `CURR`             | Current in V/100  | ADC-mux channel           | `0b111` (7) |
| 25    | `VOLT`             | Voltage in A/100  | ADC-mux channel           | `0b110` (6) |
| 26    | `RELAY`            | Default relay pin | RP2040 GPIO               | GP26 (`A0_GPIO_PIN`) |

Host indexes stop at `26`. GP27/GP28 (formerly `A1`/`A2`) are **not used** by the
firmware and **not addressable** over the protocol.

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
