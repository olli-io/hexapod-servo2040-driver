# Uart Driver for the Pimoroni Servo 2040

> [!WARNING]
> **Work in progress** — This project is under active development. APIs,
> configuration, and behavior may change without notice, and some features are
> incomplete or untested. Use at your own risk. THERE ARE NO QUARANTEES THAT THE NEWEST COMMIT RUNS.

> [!WARNING]
> **BEWARE WHEN CONNECTING TO EXT. POWER OR BATTERY**:
> When you are running servos with a higher voltage than 4V, you **need** to cut the 'Separate USB and Ext. Power' trace on the back of the board. Otherwise you may destroy the board itself or any device connected to the usb.

This driver targets the [pimoroni servo 2040 board](https://shop.pimoroni.com/products/servo-2040?variant=39800591679571), a RP2040-based 18-channel servo controller, and exposes the servos and on-board sensors over a simple binary serial protocol so any host can drive them.

**This repository is part of a multi-repo hexapod stack:**
- ROS2 hexapod controller - ['olli-io/hexapod-ros2-control'](https://github.com/olli-io/hexapod-ros2-control)
- Esp32 firmware to drive an oled screen - ['olli-io/hexapod-esp32-display'](https://github.com/olli-io/hexapod-esp32-display)

> [!NOTE]
> **This repository is a fork** of [EddieCarrera/chica-servo2039-simpleDriver](https://github.com/EddiaCarrera/chica-servo2040-simpleDriver). 
> This driver was originally made for the [MYP project](https://github.com/makeyourpet/hexapod).

> It diverges from the upstream firmware in three ways:
> - The host link defaults to **USB-CDC** but can be built for **UART on GP20/GP21** (see [Host link options](#host-link-options)).
> - The **GET reply is terminated with an MSB-set framing byte** so the host can resynchronize unambiguously.
> - A **firmware-side over-current trip** drops the servo enable when the bus current exceeds a configurable threshold.
> - The full wire protocol used by this fork is documented in [`protocol.md`](protocol.md). 

## Loading the Firmware Image
To load the firmware onto the Servo 2040 board:
1) **Read the warnings before**
2) Plug in the USB-C cable to your machine. Hold down the "boot/user" button, press the reset button at the same time, and let go of both buttons. The RP2040 should now appear as a drive to the computer.
3) Drag and drop the corresponding `.uf2` image file onto the RP2040 drive. The device will automatically reboot and start the loaded program.

The default firmware build uses USB-CDC for the host link; pass `-DHOST_LINK=UART` to cmake to produce the UART variant. See [Host link options](#host-link-options) below.

## Host link options
The firmware can be built for either USB-CDC (default) or UART. The wire protocol is identical in both modes; only the transport changes.

### USB-CDC (default)
`stdio` is routed to the USB-C port as a virtual COM port at the host's chosen baud. On startup, the LEDs perform a cyclic rainbow pattern until the host opens the CDC connection, after which the bar turns solid green.

To build the USB variant explicitly:
```
cmake -DHOST_LINK=USB ..
```

### UART on GP20/GP21
`stdio` is routed to **UART1 on GP20 (TX) and GP21 (RX)** at 115200 baud through the pins labelled BG::SDA (GPIO20) and BG::SCL (GPIO21). GP20/GP21 are the only RP2040 UART pin pair that does not collide with the Servo 2040's servo outputs (GP0–GP17), the on-board LED bar (GP18), the ADC mux / user switch (GP22–GP25), or the analog inputs (GP26–GP29). In this mode USB-CDC stdio is disabled, so the USB-C port is only used for power and for flashing `.uf2` images via BOOTSEL. Because UART has no host-side connection event, the LED bar goes straight to the solid-green "connected" state at boot.

To build the UART variant:
```
cmake -DHOST_LINK=UART ..
```

## Powering the board
The board can be powered via the pins labelled 5v and (-), or the usb connections. See 'Battery Power Warning' above for considerations.
**Recommended:** when running from a 2s lipo, use a [mini360 step down converter](https://www.google.com/search?q=mini+360+step+down+converter) set to 5V and connected to the aforementioned pins.

## Over-current Trip
The firmware samples the bus current every `OVERCURRENT_SAMPLE_US` (10 ms) and runs each sample through a tiered inverse-time protection table — higher current shortens the trip delay. When any tier's dwell exceeds its debounce, the firmware latches the servo enable off (disables all PWM outputs and de-asserts the relay). Defaults in `chica-servo2040/main.h` are sized for the 10 A continuous rating of the screw terminal block:

| Threshold | Debounce | Purpose                          |
| --------- | -------- | -------------------------------- |
| 15 A      | 0 ms     | Instant cutoff (dead short)      |
| 12 A      | 200 ms   | Hard over-stress                 |
| 11 A      | 1 s      | Sustained draw above rated load  |

When a trip latches, the LED bar turns solid red. The host can re-enable the servos by toggling the relay off (which clears the latch and returns the bar to green) and then back on once the fault condition is cleared.

## Servo calibration utility
Accurate servo positioning requires per-servo PWM calibration values, as demonstrated in MYP's [servo calibration video](https://www.youtube.com/watch?v=UMUeKFPptU4).

To calibrate servos, load the included [servo calibration firmaware](servoCalibration.uf2) to the servo2040. This utility streamlines the PWM value acquisition process: a table is produced at the end of the program, which you can copy or screenshot for later use in your host configuration. A tutorial video for using `servoCalibration.uf2` can be found [here](https://youtu.be/w5ZRXiZLpTk).

## Communication protocol
The firmware implements a thin binary protocol over the host serial link. `SET` writes pulse widths or digital outputs to one or more consecutive pins; `GET` reads the last commanded pulse, the bus voltage/current, or the touch inputs. Command bytes have the MSB set; data bytes do not — this is how the parser resynchronizes after errors. See [`protocol.md`](protocol.md) for the full byte-level specification.
