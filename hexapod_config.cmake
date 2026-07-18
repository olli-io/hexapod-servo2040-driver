# hexapod_config.cmake
#
# Single source of truth for the board-level configuration shared by every
# firmware target (chica-servo2040, servoCalibration, ...).
#
# Everything here is either a CMake cache variable — overridable on the cmake
# command line with -D<NAME>=<value> — or applied to a target as a compile
# definition by hexapod_apply_config(). The C/C++ sources read the pin numbers
# through those compile definitions and fall back to the same values when built
# outside this build system.

include_guard(GLOBAL)

# ---- Host serial link -------------------------------------------------------
# Default link used by a target that does not request one explicitly.
set(HOST_LINK "UART" CACHE STRING "Host serial link: USB or UART")
set_property(CACHE HOST_LINK PROPERTY STRINGS "USB" "UART")

# ---- UART (stdio) pins ------------------------------------------------------
# GP20/GP21 on UART1 is the only RP2040 UART pin pair that does not collide with
# the Servo2040's servo outputs (GP0-GP17), LED (GP18), ADC mux/USER_SW
# (GP22-GP25), or analog inputs (GP26-GP29).
set(HEXAPOD_UART_INSTANCE 1  CACHE STRING "RP2040 UART instance used for stdio")
set(HEXAPOD_UART_TX_PIN   20 CACHE STRING "stdio UART TX GPIO - labelled SDA on Servo2040")
set(HEXAPOD_UART_RX_PIN   21 CACHE STRING "stdio UART RX GPIO - labelled SCL on Servo2040")
# 460800 keeps 200 Hz SET+GET well inside the link budget; the SDK default is
# only 115200. Must match the host's serial baud rate.
set(HEXAPOD_UART_BAUD 460800 CACHE STRING "stdio UART baud rate")

# ---- Relay control pins -----------------------------------------------------
# A0 doubles as the primary RELAY control line, so RELAY and A0 share one pin.
set(HEXAPOD_RELAY_PIN      26 CACHE STRING "Primary relay / A0 control GPIO")

# ---- Over-current protection ------------------------------------------------
# Tiered inverse-time trip sized for a 10 A continuous rail. A tier fires when
# the rail current stays at/above its threshold_A for at least debounce_us; the
# first tier to satisfy its dwell wins. Ordered ascending by threshold, so the
# top tier (debounce 0) is the instant cutoff for dead-shorts. These are applied
# as compile definitions and mirror the self-contained fallbacks in
# src/chica-servo2040/main.h.
set(HEXAPOD_OVERCURRENT_SAMPLE_US 10000 CACHE STRING "Current-sense sample interval (us) - 10 ms = 100 Hz")

set(HEXAPOD_OVERCURRENT_TIER1_A           10.0    CACHE STRING "Tier 1 trip threshold (A) - 1.1x rated")
set(HEXAPOD_OVERCURRENT_TIER1_DEBOUNCE_US 5000000 CACHE STRING "Tier 1 dwell before trip (us) - 5 s sustained")
set(HEXAPOD_OVERCURRENT_TIER2_A           12.0    CACHE STRING "Tier 2 trip threshold (A) - 1.2x rated")
set(HEXAPOD_OVERCURRENT_TIER2_DEBOUNCE_US 500000  CACHE STRING "Tier 2 dwell before trip (us) - 500 ms")
set(HEXAPOD_OVERCURRENT_TIER3_A           15.0    CACHE STRING "Tier 3 trip threshold (A) - 1.5x rated")
set(HEXAPOD_OVERCURRENT_TIER3_DEBOUNCE_US 0       CACHE STRING "Tier 3 dwell before trip (us) - instant cutoff")

# hexapod_apply_config(<target> [LINK <USB|UART>])
#
# Applies the shared configuration to <target>: selects the stdio backend,
# wires up the UART pins, and exposes every pin number as a compile definition.
# LINK overrides the global HOST_LINK default for this target.
function(hexapod_apply_config target)
  cmake_parse_arguments(ARG "" "LINK" "" ${ARGN})
  set(link "${ARG_LINK}")
  if(NOT link)
    set(link "${HOST_LINK}")
  endif()

  # Shared definitions (read by the C/C++ sources).
  target_compile_definitions(${target} PRIVATE
    RELAY_GPIO_PIN=${HEXAPOD_RELAY_PIN}
    A0_GPIO_PIN=${HEXAPOD_RELAY_PIN}
    OVERCURRENT_SAMPLE_US=${HEXAPOD_OVERCURRENT_SAMPLE_US}
    OVERCURRENT_TIER1_A=${HEXAPOD_OVERCURRENT_TIER1_A}
    OVERCURRENT_TIER1_DEBOUNCE_US=${HEXAPOD_OVERCURRENT_TIER1_DEBOUNCE_US}
    OVERCURRENT_TIER2_A=${HEXAPOD_OVERCURRENT_TIER2_A}
    OVERCURRENT_TIER2_DEBOUNCE_US=${HEXAPOD_OVERCURRENT_TIER2_DEBOUNCE_US}
    OVERCURRENT_TIER3_A=${HEXAPOD_OVERCURRENT_TIER3_A}
    OVERCURRENT_TIER3_DEBOUNCE_US=${HEXAPOD_OVERCURRENT_TIER3_DEBOUNCE_US}
  )

  if(link STREQUAL "USB")
    pico_enable_stdio_usb(${target} 1)
    pico_enable_stdio_uart(${target} 0)
    target_compile_definitions(${target} PRIVATE HOST_LINK_USB=1)
  elseif(link STREQUAL "UART")
    pico_enable_stdio_usb(${target} 0)
    pico_enable_stdio_uart(${target} 1)
    target_compile_definitions(${target} PRIVATE
      HOST_LINK_UART=1
      PICO_DEFAULT_UART=${HEXAPOD_UART_INSTANCE}
      PICO_DEFAULT_UART_TX_PIN=${HEXAPOD_UART_TX_PIN}
      PICO_DEFAULT_UART_RX_PIN=${HEXAPOD_UART_RX_PIN}
      PICO_DEFAULT_UART_BAUD_RATE=${HEXAPOD_UART_BAUD}
    )
  else()
    message(FATAL_ERROR "HOST_LINK must be USB or UART (got '${link}')")
  endif()
endfunction()
