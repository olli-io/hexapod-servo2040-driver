# Vendored third-party source

The `common/`, `drivers/`, and `libraries/` trees are copied (not submoduled)
from Pimoroni's [`pimoroni-pico`](https://github.com/pimoroni/pimoroni-pico)
repository.

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

- `common/` — shared helpers (`pimoroni_common.hpp` etc.)
- `drivers/servo` — `servo`, `servo_cluster`
- `drivers/pwm` — `pwm`, `pwm_cluster` (PIO/DMA PWM engine)
- `drivers/plasma` — `WS2812` LED driver (PIO/DMA)
- `drivers/analog` — ADC wrapper
- `drivers/analogmux` — ADC multiplexer
- `drivers/button` — button helper
- `libraries/servo2040` — Servo 2040 board pin map + `ServoCluster` glue

`drivers/CMakeLists.txt` and `libraries/CMakeLists.txt` are **first-party**
(trimmed to the kept subset), not copied from upstream.

## Updating the vendored source

Re-copy the subset above from a newer `pimoroni-pico` tag, keep the trimmed
`CMakeLists.txt` files, and update the tag/commit in this file. Verify the tag's
CI targets a Pico SDK version matching the one pinned in the `Dockerfile`.
