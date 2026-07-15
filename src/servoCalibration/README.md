# Servo Calibration

Each servo needs its own PWM values so the hexapod knows the exact pulse width
for each angle. This utility walks you through every servo and records those
values for you.

At the end it prints a table you can copy into your host configuration.

There is also a [tutorial video](https://youtu.be/w5ZRXiZLpTk) that shows the
whole process.

## What you need

- The Servo 2040 board with all servos plugged in and powered.
- A USB-C cable between the board and your computer.
- A serial terminal on your computer (for example `screen`, `minicom`, `tio`,
  or PuTTY). This utility talks over **USB**.

> [!WARNING]
> If you power the servos with more than 4V, you **must** cut the
> 'Separate USB and Ext. Power' trace on the back of the board first.
> Otherwise you can destroy the board or your computer's USB port.

## Steps

1. **Load the firmware.** Put the board into BOOTSEL mode (hold *boot/user*,
   press *reset*, release both) and drag
   [`dist/servoCalibration.uf2`](../../dist/servoCalibration.uf2) onto the drive
   that appears. See the [root README](../../README.md#loading-the-firmware-image)
   for details.

2. **Open a serial terminal.** Connect to the board's serial port. For example:
   ```
   screen /dev/ttyACM0
   ```
   (On Linux the port is usually `/dev/ttyACM0`. On Windows it is a `COM` port.)

3. **Start.** Read the on-screen warning, then press any key. All servos move to
   an uncalibrated center. The tool then steps through each servo, one at a time.

4. **Calibrate each servo.** For every servo you set two positions:
   - First move it to **-45°** (counter-clockwise / left).
   - Then move it to **+45°** (clockwise / right).

   Use the arrow keys to nudge the servo until it is exactly in position, then
   press `s` to save. The tool automatically moves on to the next position, and
   then the next servo.

   | Key | Action        |
   | --- | ------------- |
   | ↑   | PWM +20 µs    |
   | ↓   | PWM -20 µs    |
   | →   | PWM +5 µs     |
   | ←   | PWM -5 µs     |
   | s   | save position |

   The current PWM value is shown on screen as you adjust it.

5. **Finish.** After all servos are done, the tool centers every servo using the
   values you recorded. **Look at the servos** — they should all be exactly
   centered. If any are off, run the calibration again for those servos.

6. **Copy the results.** The tool prints a table like this:
   ```yaml
   calibration_values:
     - { pin: 1, us_at_plus_45: 1950, us_at_minus_45: 1050 }
     - { pin: 2, us_at_plus_45: 1980, us_at_minus_45: 1020 }
     ...
   ```
   Copy or screenshot it and paste it into your host configuration.

## Notes

- PWM values are limited to the safe range **500–2500 µs**.
- To calibrate only some servos, change `START_PIN` and `END_PIN` in
  [`servoCalibration.cpp`](servoCalibration.cpp) and rebuild.
