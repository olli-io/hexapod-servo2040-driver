#!/usr/bin/env bash
#
# Build the Servo 2040 firmware in the pinned Docker toolchain (see Dockerfile)
# and stage the resulting .uf2 images in dist/.
#
# Usage:
#   ./build.sh [--link USB|UART] [--clean] [target ...]
#
#   ./build.sh                      # both targets, UART host link (default)
#   ./build.sh --link USB           # USB-CDC variant
#   ./build.sh servoCalibration     # just the calibrator
#   ./build.sh --clean              # drop the build tree and reconfigure
#   ./build.sh -DHEXAPOD_UART_BAUD=115200
#
# -D... arguments are forwarded to the cmake configure step (see
# hexapod_config.cmake for the available cache variables); any other bare
# argument is a cmake target name.

set -euo pipefail

readonly IMAGE=servo2040-build
readonly BUILD_DIR=build
readonly DIST_DIR=dist

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$repo_root"

link=UART
clean=0
targets=()
defines=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --link)
      [[ $# -ge 2 ]] || { echo "build.sh: --link needs USB or UART" >&2; exit 2; }
      link="$2"
      shift 2
      ;;
    --clean)
      clean=1
      shift
      ;;
    -h|--help)
      awk 'NR<3 {next} /^#/ {sub(/^# ?/, ""); print; next} {exit}' "${BASH_SOURCE[0]}"
      exit 0
      ;;
    -D*)
      defines+=("$1")
      shift
      ;;
    -*)
      echo "build.sh: unknown option '$1'" >&2
      exit 2
      ;;
    *)
      targets+=("$1")
      shift
      ;;
  esac
done

case "$link" in
  USB|UART) ;;
  *) echo "build.sh: --link must be USB or UART (got '$link')" >&2; exit 2 ;;
esac

# A build tree from a pre---user run is owned by root and cannot be written to
# or removed as the invoking user. Say so plainly rather than failing deep
# inside cmake with a permission error.
if [[ -d $BUILD_DIR && ! -w $BUILD_DIR ]]; then
  cat >&2 <<EOF
build.sh: '$BUILD_DIR/' is not writable by $(id -un) — it was most likely
created by an older 'docker run' without --user, so it is owned by root.
Remove it once and rebuild:

    sudo rm -rf $BUILD_DIR

EOF
  exit 1
fi

if [[ $clean -eq 1 ]]; then
  echo "==> removing $BUILD_DIR/"
  rm -rf "${BUILD_DIR:?}"
fi

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  echo "==> building image $IMAGE (one-time, several minutes)"
  docker build -t "$IMAGE" .
fi

# The container runs the two cmake steps under `sh -c`, so every caller-supplied
# word is escaped with %q rather than interpolated raw.
configure=(cmake -S . -B "$BUILD_DIR" -DHOST_LINK="$link" ${defines[@]+"${defines[@]}"})
compile=(cmake --build "$BUILD_DIR")
for target in ${targets[@]+"${targets[@]}"}; do
  compile+=(--target "$target")
done

build_cmd="$(printf '%q ' "${configure[@]}")&& $(printf '%q ' "${compile[@]}")"
# Left unquoted on purpose: nproc must expand inside the container, not here.
build_cmd+=' -j"$(nproc)"'

echo "==> building (host link: $link${targets[0]+, targets: ${targets[*]}})"
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$repo_root:/project" \
  "$IMAGE" \
  sh -c "$build_cmd"

# Stage the images. The build tree is disposable and gitignored; dist/ is what
# gets committed and linked from the README.
mkdir -p "$DIST_DIR"
shopt -s nullglob
staged=0
for uf2 in "$BUILD_DIR"/src/*/*.uf2; do
  cp "$uf2" "$DIST_DIR/"
  echo "==> staged $DIST_DIR/$(basename "$uf2")"
  staged=$((staged + 1))
done

if [[ $staged -eq 0 ]]; then
  echo "build.sh: no .uf2 images found under $BUILD_DIR/src" >&2
  exit 1
fi
