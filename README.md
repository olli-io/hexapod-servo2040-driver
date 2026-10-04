# UART Driver for the Pimoroni Servo 2040

Firmware for the [Pimoroni Servo 2040](https://shop.pimoroni.com/products/servo-2040?variant=39800591679571) (RP2040, 18 servo channels). It exposes the servos and on-board sensors to a host over a simple binary serial protocol.

> [!WARNING]
> **External power / battery:** if the servos run at more than 4 V, you **must** cut the 'Separate USB and Ext. Power' trace on the back of the board. If you do not, you can destroy the board or the device connected to the USB.

**Prebuilt firmware images:**
- [`dist/servoCalibration.uf2`](dist/servoCalibration.uf2) — servo calibration utility
- [`dist/hexapod-servo2040-firmware.uf2`](dist/hexapod-servo2040-firmware.uf2) — main driver firmware (UART host link)

## 1. Servo calibration utility
Each servo needs its own PWM calibration values for accurate positioning (see MYP's [servo calibration video](https://www.youtube.com/watch?v=UMUeKFPptU4)).

1. Load [`servoCalibration.uf2`](dist/servoCalibration.uf2) onto the board (see [Loading firmware](#2-loading-firmware)).
2. Follow the instructions in [`src/servoCalibration/README.md`](src/servoCalibration/README.md). Tutorial video: [here](https://youtu.be/w5ZRXiZLpTk).
3. At the end, the utility shows a table of PWM values. Copy or screenshot it for your host configuration.

## 2. Loading firmware
1. Read the warnings above.
2. Connect the board to your computer with a USB-C cable.
3. Hold the "boot/user" button, push the reset button, then release both. The RP2040 shows as a drive.
4. Drag the `.uf2` file onto the drive. The board reboots and starts the firmware.

The prebuilt main firmware uses UART on GP20/GP21. For USB-CDC, build with `--link USB` (see [Host link options](#host-link-options)).

## Powering the board
Supply power through the `5v` and `(-)` pins, or through USB. Read the power warning above.
**Recommended:** with a 2S LiPo, use a [mini360 step-down converter](https://www.google.com/search?q=mini+360+step+down+converter) set to 5 V on these pins.

## Companion repositories
- Hexapod build instructions (main repo): [olli-io/hexapod](https://github.com/olli-io/hexapod)
- ROS2 hexapod controller: [olli-io/hexapod-ros2-control](https://github.com/olli-io/hexapod-ros2-control)

---

## Building the firmware
Build only if you change the configuration or the sources. The build runs in Docker (toolchain, Pico SDK and picotool are pinned in the [`Dockerfile`](Dockerfile)). The host needs only Docker.

```
./build.sh                      # both targets, UART host link (default)
./build.sh --link USB           # USB-CDC variant
./build.sh servoCalibration     # a single cmake target
./build.sh --clean              # discard the build tree and reconfigure
```

The script compiles into `build/` and copies the `.uf2` images into `dist/`. The container runs as your UID, so `build/` stays writable without `sudo`.

Board settings (UART pins and baud, relay GPIO, over-current tiers) are cache variables in [`hexapod_config.cmake`](hexapod_config.cmake). Override with cmake flags, e.g. `-DHEXAPOD_UART_BAUD=115200`.

## Host link options
The wire protocol is the same for both links. Only the transport changes.

**UART on GP20/GP21 (default, `--link UART`):** UART1, TX on GP20 (BG::SDA), RX on GP21 (BG::SCL), 115200 baud. This is the only RP2040 UART pin pair that does not collide with the servo outputs, LED bar, ADC mux or analog inputs. USB is used only for power and flashing. The LED bar goes solid green at boot.

**USB-CDC (`--link USB`):** a virtual COM port on the USB-C port. The LEDs show a rainbow pattern until the host opens the port, then go solid green.

## Over-current trip
The firmware samples the bus current every `OVERCURRENT_SAMPLE_US` (10 ms) and uses a tiered inverse-time table. When a tier's dwell exceeds its debounce, the firmware latches the servo enable off (all PWM outputs off, relay off). Defaults in `src/hexapod-servo2040-firmware/main.h` match the 10 A rating of the screw terminal:

| Threshold | Debounce | Purpose                          |
| --------- | -------- | -------------------------------- |
| 15 A      | 0 ms     | Instant cutoff (dead short)      |
| 12 A      | 200 ms   | Hard over-stress                 |
| 11 A      | 1 s      | Sustained draw above rated load  |

On a trip, the LED bar goes solid red. To recover, clear the fault, send `SET RELAY 1`, then send new `SET`s to the servos. The board **does not restore the pre-trip positions**. `SET RELAY 1` powers the rail with all servos limp and never moves a servo. See [`protocol.md`](protocol.md#energizing-servos-relay-first-host-ordered) for the full bring-up sequence.

## Communication protocol
A thin binary protocol on the host link. `SET` writes pulse widths or digital outputs to one or more consecutive pins. `GET` reads the last commanded pulse, bus voltage/current, or touch inputs. Command bytes have the MSB set; data bytes do not, so the parser can resynchronize after errors.

Battery telemetry (`GET` on CURR/VOLT) is in centi-units: `count = round(value * 100)`. Multiply by `0.01` to get A or V. Touch-sensor `GET`s return raw ADC-derived codes.

Full byte-level specification: [`protocol.md`](protocol.md).

## Fork origin
This is a fork of [EddieCarrera/chica-servo2040-simpleDriver](https://github.com/EddieCarrera/chica-servo2040-simpleDriver), originally made for the [MYP project](https://github.com/makeyourpet/hexapod). Differences from upstream:
- Host link defaults to UART on GP20/GP21; USB-CDC is optional.
- The GET reply ends with an MSB-set framing byte for unambiguous resync.
- Firmware-side over-current trip.
