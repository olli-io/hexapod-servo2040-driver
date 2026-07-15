set(OUTPUT_NAME servoCalibration)
add_executable(${OUTPUT_NAME} servoCalibration.cpp)

target_link_libraries(${OUTPUT_NAME}
        pico_stdlib
        servo2040
        analogmux
        analog
        button
        )

# Force the USB link (disable UART) so calibration output doesn't confuse any
# connected servos. Pin numbers still come from hexapod_config.cmake.
hexapod_apply_config(${OUTPUT_NAME} LINK USB)

pico_add_extra_outputs(${OUTPUT_NAME})
