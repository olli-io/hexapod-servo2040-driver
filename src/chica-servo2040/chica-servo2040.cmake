set(OUTPUT_NAME chica-servo2040)
add_executable(${OUTPUT_NAME} chica-servo2040.cpp)

target_link_libraries(${OUTPUT_NAME}
        pico_stdlib
        servo2040
        analogmux
        analog
        button
        )

# Host link, UART pins and GPIO pins all come from hexapod_config.cmake. The
# default link is UART-on-GP20/GP21; pass `-DHOST_LINK=USB` to cmake to build
# the USB-CDC variant instead.
hexapod_apply_config(${OUTPUT_NAME})

pico_add_extra_outputs(${OUTPUT_NAME})
