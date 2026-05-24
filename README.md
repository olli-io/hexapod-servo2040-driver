# Uart Driver for the Pimoroni Servo 2040
This driver targets [Pimoroni's Servo 2040 board](https://shop.pimoroni.com/products/servo-2040?variant=39800591679571), a RP2040-based 18-channel servo controller, and exposes the servos and on-board sensors over a simple binary serial protocol so any host can drive them.

> **This repository is a fork** of [EddieCarrera/chica-servo2040-simpleDriver](https://github.com/EddiaCarrera/chica-servo2040-simpleDriver). 

> It diverges from the upstream firmware in three ways:
> - The host link is **UART on GP20/GP21** instead of USB CDC.
> - The **GET reply is terminated with an MSB-set framing byte** so the host can resynchronize unambiguously.
> - A **firmware-side over-current trip** drops the servo enable when the bus current exceeds a configurable threshold.

> The full wire protocol used by this fork is documented in [`protocol.md`](protocol.md).

### ***External Power Warning***:
This application requires an external power source greater than 5V to power the servos through the terminal block of the board.  **If you want to run servos with a higher voltage than 5V, you need to _cut the 'Separate USB and Ext. Power' trace on the back of the board_ to prevent the RP2040 or _your machine_ being damaged by the increased voltage.**

## Loading the Firmware Image
To load the firmware onto the Servo 2040 board:
1) **Read the warnings below.**

2) Plug in the USB-C cable to your machine. Hold down the "boot/user" button, press the reset button at the same time, and let go of both buttons. The RP2040 should now appear as a drive to the computer.

3) Drag and drop the corresponding `.uf2` image file onto the RP2040 drive. The device will automatically reboot and start the loaded program.

# Hardware
## Servo 2040

### [Schematics](https://cdn.shopify.com/s/files/1/0174/1800/files/servo2040_schematic.pdf?v=1648817752)

Details from the [Pimoroni's website](https://shop.pimoroni.com/products/servo-2040?variant=39800591679571):
> The servo 2040 is a standalone servo controller for making things with lots of moving parts. It has pre-soldered pin headers for plugging in up to 18 servos - enough for the leggiest of hexapod walkers or plenty of degrees of freedom for your robotic arms, legs or tentacles. Servos can be pretty power hungry, especially the chunky ones, so we've added some neat current monitoring functions so you can keep an eye on power consumption.

>We've used RP2040 as the core of this board because of the flexibility of its Programmable IOs (PIOs). Traditionally, each servo needs to be connected to its own PWM capable channel on the microcontroller. RP2040 only has 16 PWM channels, but it's possible to drive up to 30 servos using the magic of PIOs (if you're canny with wiring). RP2040's PIOs are also super fast, so they can drive servos with sub microsecond resolution.

Keep in mind that the screw terminals for supplying external power (with reverse polarity protection) are rated for 10A max continuous current.

### ***Battery Power Warning***:
Although this application doesn't require it, the Servo 2040 can be powered by a 5V battery through the 5V/GND pins, instead of through the USB-C port. **When sourcing power through the 5V pins, it's important to _use a data-only USB adapter/cable_ to program the board.** This will prevent the 5V battery from backfeeding to the 5V source provided by the USB device.

## Hexapod Robot Build
## Servos
The two most commonly recommended servos for this style of build are the [ZOSKAY 35kg coreless servos](https://www.amazon.com/dp/B07SBYZ4G5?_encoding=UTF8&ref_=cm_sw_r_cp_ud_dp_FHYWJWD1TXGTMJDHJMWC&th=1) [[Alternate Link]](https://www.aliexpress.us/item/2251832824472591.html?spm=a2g0o.order_detail.0.0.2e03f19c3p5o3j&gatewayAdapt=glo2usa4itemAdapt&_randl_shipto=US) and the [Feetech 35kg cored servos](https://www.robotshop.com/products/feetech-180-degrees-digital-servo-74v-35kg-cm-ft5330m). The reference hexapod chassis (linked below) is designed around the ZOSKAY servos.

## 3D Printed Chassis
All 3D printed designs, BOM, wiring guides, and build information for the reference hexapod can be found on Make Your Pet's [hexapod repository](https://github.com/MakeYourPet/hexapod).

# Software
The firmware implements a thin binary protocol over the host serial link. `SET` writes pulse widths or digital outputs to one or more consecutive pins; `GET` reads the last commanded pulse, the bus voltage/current, or the touch inputs. Command bytes have the MSB set; data bytes do not — this is how the parser resynchronizes after errors. See [`protocol.md`](protocol.md) for the full byte-level specification, including the trailing framing byte appended to `GET` replies in this fork.

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
