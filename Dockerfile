# Reproducible build environment for the Servo 2040 firmware.
#
# The image ships the ARM bare-metal toolchain and a pinned copy of the
# Raspberry Pi Pico SDK. The firmware sources are bind-mounted at build time
# rather than baked in, so the same image rebuilds any commit.
#
# Build the image:
#   docker build -t servo2040-build .
#
# Build the firmware (from the repo root), output lands in ./build:
#   docker run --rm -v "$PWD:/project" servo2040-build \
#     sh -c 'cmake -S . -B build && cmake --build build -j"$(nproc)"'
#
# Build just the calibrator:
#   docker run --rm -v "$PWD:/project" servo2040-build \
#     sh -c 'cmake -S . -B build && cmake --build build --target servoCalibration -j"$(nproc)"'
#
# USB-CDC host-link variant: add -DHOST_LINK=USB to the cmake configure step.

FROM debian:bookworm-slim

# Pin the SDK so builds are reproducible. 1.5.1 pairs with the vendored
# pimoroni-pico libraries and needs no external picotool for the .uf2 stage.
ARG PICO_SDK_TAG=1.5.1

RUN apt-get update && apt-get install --no-install-recommends -y \
        cmake \
        gcc-arm-none-eabi \
        libnewlib-arm-none-eabi \
        libstdc++-arm-none-eabi-newlib \
        build-essential \
        python3 \
        git \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Clone the SDK (with submodules) at the pinned tag.
ENV PICO_SDK_PATH=/opt/pico-sdk
RUN git clone --branch "${PICO_SDK_TAG}" --depth 1 \
        https://github.com/raspberrypi/pico-sdk.git "${PICO_SDK_PATH}" \
    && git -C "${PICO_SDK_PATH}" submodule update --init --depth 1

WORKDIR /project

CMD ["sh", "-c", "cmake -S . -B build && cmake --build build -j\"$(nproc)\""]
