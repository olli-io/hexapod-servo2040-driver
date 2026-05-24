# Uart Driver for the Pimoroni Servo 2040
This driver targets [Pimoroni's Servo 2040 board](https://shop.pimoroni.com/products/servo-2040?variant=39800591679571), a RP2040-based 18-channel servo controller, and exposes the servos and on-board sensors over a simple binary serial protocol so any host can drive them.

> **This repository is a fork** of [EddieCarrera/chica-servo2040-simpleDriver](https://github.com/EddiaCarrera/chica-servo2040-simpleDriver). 

> It diverges from the upstream firmware in three ways:
> - The host link is **UART on GP20/GP21** instead of USB CDC.
> - The **GET reply is terminated with an MSB-set framing byte** so the host can resynchronize unambiguously.
> - A **firmware-side over-current trip** drops the servo enable when the bus current exceeds a configurable threshold.

> The full wire protocol used by this fork is documented in [`protocol.md`](protocol.md).

### ***External Power Warning***:
**When you are running servos with a higher voltage than 5V, you need to _cut the 'Separate USB and Ext. Power' trace on the back of the board_ to prevent the RP2040 or _your machine_ being damaged by the increased voltage.**
**The board is rated for 10A continuous power**
### ***Battery Power Warning***:
If the Servo 2040 can be powered by a 5V battery through the 5V/GND pins, instead of through the USB-C port. **When sourcing power through the 5V pins, it's important to _use a data-only USB adapter/cable_ to program the board.** This will prevent the 5V battery from backfeeding to the 5V source provided by the USB device. You can you a normal cable,
but the 5V/GND pins must be removed prior to connecting the powered usb.

## Loading the Firmware Image
To load the firmware onto the Servo 2040 board:
1) **Read the warnings before**
2) Plug in the USB-C cable to your machine. Hold down the "boot/user" button, press the reset button at the same time, and let go of both buttons. The RP2040 should now appear as a drive to the computer.
3) Drag and drop the corresponding `.uf2` image file onto the RP2040 drive. The device will automatically reboot and start the loaded program.

# Hardware
## Pimoroni servo2040
Schematics and details from pimoroni:
[Schematics](https://cdn.shopify.com/s/files/1/0174/1800/files/servo2040_schematic.pdf?v=1648817752)
[Servo2040](https://shop.pimoroni.com/products/servo-2040?variant=39800591679571):

# Software
The firmware implements a thin binary protocol over the host serial link. `SET` writes pulse widths or digital outputs to one or more consecutive pins; `GET` reads the last commanded pulse, the bus voltage/current, or the touch inputs. Command bytes have the MSB set; data bytes do not — this is how the parser resynchronizes after errors. See [`protocol.md`](protocol.md) for the full byte-level specification. This driver was originally made for the [MYP project](https://github.com/makeyourpet/hexapod). This is fork with minor changes to the original protocol.

## Features
### UART Host Link (GP20/GP21)
The host link is `stdio` routed to **UART1 on GP20 (TX) and GP21 (RX)** at 115200 baud. GP20/GP21 are the only RP2040 UART pin pair that does not collide with the Servo 2040's servo outputs (GP0–GP17), the on-board LED bar (GP18), the ADC mux / user switch (GP22–GP25), or the analog inputs (GP26–GP29). USB CDC stdio is disabled, so the USB-C port is only used for power and for flashing `.uf2` images via BOOTSEL.

Upon startup, the LEDs perform a cyclic rainbow pattern until a host connection is established.

### Virtual Servo Power Relay
When the host de-asserts the servo power relay pin, the firmware also disables the PWM signal outputs on all servos, which removes torque from the servos. This makes a physical servo power relay optional for the hexapod build.

### Over-current Trip
The firmware samples the bus current at a fixed interval and, if the current stays above a configured threshold for the full debounce window, it latches the servo enable off (disables all PWM outputs and de-asserts the relay). The defaults — `OVERCURRENT_THRESHOLD_A = 10.0` A, `OVERCURRENT_DEBOUNCE_US = 1 s`, `OVERCURRENT_SAMPLE_US = 10 ms` — are set in `chica-servo2040/main.h` and match the 10 A continuous rating of the screw terminal block. The host can re-enable the servos by re-asserting the relay once the fault condition is cleared.

### Tools
Accurate servo positioning requires per-servo PWM calibration values, as demonstrated in MYP's [servo calibration video](https://www.youtube.com/watch?v=UMUeKFPptU4).

The `ServoCalibration` directory contains `servoCalibration.uf2`, along with the `.stl` files for the physical components needed for calibration (provided by MYP). This utility streamlines the PWM value acquisition process: a table is produced at the end of the program, which you can copy or screenshot for later use in your host configuration. All done without needing to buy a separate servo calibrator. [A tutorial video for using `servoCalibration.uf2` can be found here](https://youtu.be/w5ZRXiZLpTk).

# Development
## Dependencies
This application is written in C++ for speed and performance.

The [pico-SDK](https://github.com/raspberrypi/pico-sdk) and [pimoroni-pico](https://github.com/pimoroni/pimoroni-pico) are required libraries for Servo 2040 development. It's recommended to start by running a "hello world" example using pico-SDK, then slowly modifying CMake files and dependencies to migrate the pimoroni-pico library into your development environment (this is the tricky part!) and implementing the [C++ pimoroni servo2040 examples](https://github.com/pimoroni/pimoroni-pico/tree/main/examples/servo2040) one at a time.

Development on the Servo 2040 can also be done using MicroPython, but this is outside the scope of this repository. A tutorial for setting up a MicroPython development environment can be found [here](https://github.com/pimoroni/pimoroni-pico/blob/main/setting-up-micropython.md).

# Community & Feedback
The upstream project is part of an active hexapod robotics community. If you would like to build your own hexapod and join the community, consider the [discord channel](https://discord.gg/vb8YWMfBuk) linked from the upstream repository.

Bug reports, feature requests, and general feedback for this fork are welcome.
