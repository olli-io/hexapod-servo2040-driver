# Reproducible build environment for the Servo 2040 firmware.
#
# The image ships the ARM bare-metal toolchain, a pinned copy of the Raspberry
# Pi Pico SDK, and a matching pinned picotool. The firmware sources are
# bind-mounted at build time rather than baked in, so the same image rebuilds
# any commit.
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

# Pin the SDK so builds are reproducible. 2.1.1 pairs with the vendored
# pimoroni-pico v1.25.0 libraries (see VENDOR.md). SDK 2.x produces the .uf2 via
# picotool, so a matching picotool is pinned and pre-built below.
ARG PICO_SDK_TAG=2.1.1
ARG PICOTOOL_TAG=2.1.1

RUN apt-get update && apt-get install --no-install-recommends -y \
        cmake \
        gcc-arm-none-eabi \
        libnewlib-arm-none-eabi \
        libstdc++-arm-none-eabi-newlib \
        build-essential \
        python3 \
        git \
        ca-certificates \
        libusb-1.0-0-dev \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Clone the SDK (with submodules) at the pinned tag.
ENV PICO_SDK_PATH=/opt/pico-sdk
RUN git clone --branch "${PICO_SDK_TAG}" --depth 1 \
        https://github.com/raspberrypi/pico-sdk.git "${PICO_SDK_PATH}" \
    && git -C "${PICO_SDK_PATH}" submodule update --init --depth 1

# Build and install picotool at the matching tag. SDK 2.x's
# pico_add_extra_outputs -> pico_add_uf2_output resolves it via
# find_package(picotool); installing to /usr/local makes it discoverable, so the
# per-build cmake step needs no network to fetch/build picotool itself.
RUN git clone --branch "${PICOTOOL_TAG}" --depth 1 \
        https://github.com/raspberrypi/picotool.git /tmp/picotool \
    && cmake -S /tmp/picotool -B /tmp/picotool/build \
        -DPICO_SDK_PATH="${PICO_SDK_PATH}" \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --build /tmp/picotool/build -j"$(nproc)" \
    && cmake --install /tmp/picotool/build \
    && rm -rf /tmp/picotool

WORKDIR /project

CMD ["sh", "-c", "cmake -S . -B build && cmake --build build -j\"$(nproc)\""]
