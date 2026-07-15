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

# ---- Relay control pins -----------------------------------------------------
# A0 doubles as the primary RELAY control line, so RELAY and A0 share one pin.
# GP27/GP28 are reserved as alternative relay control lines for future use; they
# are held low at init and are not yet exposed on the host protocol.
set(HEXAPOD_RELAY_PIN      26 CACHE STRING "Primary relay / A0 control GPIO")
set(HEXAPOD_RELAY_ALT1_PIN 27 CACHE STRING "Reserved alternative relay GPIO")
set(HEXAPOD_RELAY_ALT2_PIN 28 CACHE STRING "Reserved alternative relay GPIO")

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

  # Shared pin definitions (read by the C/C++ sources).
  target_compile_definitions(${target} PRIVATE
    RELAY_GPIO_PIN=${HEXAPOD_RELAY_PIN}
    A0_GPIO_PIN=${HEXAPOD_RELAY_PIN}
    RELAY_ALT1_GPIO_PIN=${HEXAPOD_RELAY_ALT1_PIN}
    RELAY_ALT2_GPIO_PIN=${HEXAPOD_RELAY_ALT2_PIN}
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
    )
  else()
    message(FATAL_ERROR "HOST_LINK must be USB or UART (got '${link}')")
  endif()
endfunction()
