# Trimmed to only the drivers the firmware links (directly or transitively).
# See VENDOR.md for the pimoroni-pico revision these are vendored from.
#
# Order matters: pwm must precede servo. servo_cluster.cmake pulls in pwm_cluster
# via a `if(NOT TARGET pwm_cluster)` guard, so pwm/pwm_cluster must already be
# defined by the time servo is processed to avoid a duplicate-target error.
add_subdirectory(pwm)        # pwm + pwm_cluster (PIO/DMA)
add_subdirectory(servo)      # servo + servo_cluster (uses pwm_cluster)
add_subdirectory(plasma)     # WS2812 status LEDs (PIO/DMA)
add_subdirectory(analog)     # ADC current/voltage sensing
add_subdirectory(analogmux)  # ADC multiplexer
add_subdirectory(button)     # on-board button
