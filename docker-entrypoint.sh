#!/bin/sh
#
# Refuse to build as UID 0.
#
# The sources are bind-mounted, and a bind mount does not remap UIDs: whatever
# UID the container writes as owns the files on the host. Building as root
# leaves build/ owned by root, which the invoking user then cannot rebuild into
# or delete without sudo. Fail in a second here rather than after a full
# compile. Callers should pass --user "$(id -u):$(id -g)"; ./build.sh does.
#
# Escape hatch: under rootless Docker or Podman, UID 0 inside the container is
# already mapped to the unprivileged host user, so files land correctly owned
# and this guard is a false positive. Set SERVO2040_ALLOW_ROOT=1 to skip it.

set -eu

if [ "$(id -u)" = 0 ] && [ "${SERVO2040_ALLOW_ROOT:-0}" != 1 ]; then
    cat >&2 <<'EOF'
refusing to build as root.

The build directory is bind-mounted from the host, so building as UID 0 would
leave it root-owned and unwritable by your user. Re-run as yourself:

    docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/project" servo2040-build

or just use ./build.sh, which passes that for you.

If you are on rootless Docker or Podman, container root is already mapped to
your host user and this check is a false positive: set SERVO2040_ALLOW_ROOT=1.
EOF
    exit 1
fi

exec "$@"
