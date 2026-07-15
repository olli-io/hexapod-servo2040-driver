# Vendored third-party source

The `vendor/common/`, `vendor/drivers/`, and `vendor/libraries/` trees are
copied (not submoduled) from Pimoroni's
[`pimoroni-pico`](https://github.com/pimoroni/pimoroni-pico) repository.

| Field | Value |
| ----- | ----- |
| Upstream | https://github.com/pimoroni/pimoroni-pico |
| Tag | `v1.25.0` |
| Commit | `21ec6a23ba494311ee629b97f8ee5dc68a014384` |
| Pairs with Pico SDK | **2.1.1** (this tag's CI builds against 2.1.1) |

## What is vendored

Only the subset the firmware links is kept (the upstream repo also ships
displays, wifi, graphics, fatfs, etc. — all unused here and intentionally
omitted):

- `vendor/common/` — shared helpers (`pimoroni_common.hpp` etc.)
- `vendor/drivers/servo` — `servo`, `servo_cluster`
- `vendor/drivers/pwm` — `pwm`, `pwm_cluster` (PIO/DMA PWM engine)
- `vendor/drivers/plasma` — `WS2812` LED driver (PIO/DMA)
- `vendor/drivers/analog` — ADC wrapper
- `vendor/drivers/analogmux` — ADC multiplexer
- `vendor/drivers/button` — button helper
- `vendor/libraries/servo2040` — Servo 2040 board pin map + `ServoCluster` glue

`vendor/drivers/CMakeLists.txt` and `vendor/libraries/CMakeLists.txt` are
**first-party** (trimmed to the kept subset), not copied from upstream.

## Updating the vendored source

Re-copy the subset above from a newer `pimoroni-pico` tag, keep the trimmed
`CMakeLists.txt` files, and update the tag/commit in this file. Verify the tag's
CI targets a Pico SDK version matching the one pinned in the `Dockerfile`.
