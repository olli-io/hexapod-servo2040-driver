# Reproducible build environment for the Servo 2040 firmware.
#
# The image ships the ARM bare-metal toolchain, a pinned copy of the Raspberry
# Pi Pico SDK, and a matching pinned picotool. The firmware sources are
# bind-mounted at build time rather than baked in, so the same image rebuilds
# any commit.
#
# Prefer ./build.sh over driving docker by hand — it builds the image on demand
# and passes the flags below for you:
#   ./build.sh                 # both targets, UART host link
#   ./build.sh --link USB      # USB-CDC variant
#   ./build.sh servoCalibration
#
# The equivalent by hand, from the repo root:
#   docker build -t servo2040-build .
#   docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/project" servo2040-build
#
# --user is not optional. The bind mount shares the host filesystem without
# translating UIDs, so whatever UID the container writes as is the owner on the
# host: run as the image's default root and ./build becomes a root-owned tree
# that your unprivileged user cannot rebuild into or delete without sudo. The
# image deliberately declares no USER of its own — hardcoding a UID here would
# only match whoever happened to build the image — so the caller supplies it.

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

# Make the image usable by any --user UID, not just root. An arbitrary UID has
# no /etc/passwd entry, so its HOME resolves to "/" (unwritable): point it at
# /tmp instead, which cmake and gcc need for scratch files. The SDK checkouts
# are owned by root from the build above, and git refuses to read a repo owned
# by another user; pico_sdk_import.cmake shells out to git, so mark them safe.
# Scoped to this throwaway build image, not a pattern for a runtime image.
ENV HOME=/tmp
RUN git config --system --add safe.directory "${PICO_SDK_PATH}" \
    && git config --system --add safe.directory "${PICO_SDK_PATH}/*"

WORKDIR /project

# The entrypoint refuses to run as UID 0 so a --user-less `docker run` cannot
# silently recreate a root-owned build tree. See the script for the rootless
# Docker/Podman escape hatch.
COPY docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
RUN chmod 755 /usr/local/bin/docker-entrypoint.sh

ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]
CMD ["sh", "-c", "cmake -S . -B build && cmake --build build -j\"$(nproc)\""]
