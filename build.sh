#!/bin/sh
# hologram POSIX build: Linux (OpenGL) and macOS (Metal). On Windows use
# build.bat -- MSVC is the supported toolchain there.
#   ./build.sh        build the examples into build/
#   ./build.sh test   build and run the host tests
#
# No library step and no package manager, matching build.bat: a game compiles
# hologram's sources directly, magnolia-style.

set -eu

CC=${CC:-cc}
UNAME=$(uname -s)

# The pure modules: no sokol, no window, safe to link into tests and console
# tools alike -- and portable with no work, which is why the tests below run
# on any platform before a single graphics call is ported.
PURE="source/linalg.c source/polar.c source/geometry.c source/camera.c \
source/collision.c source/cpu_trace.c source/gpu_scene.c \
source/scene_json.c source/spectrum.c source/timestep.c"

# -Wall -Wextra is the rule in AGENTS.md; it applies to hologram's own code.
WARN="-Wall -Wextra"
CFLAGS="-std=c11 -O2"

case "$UNAME" in
Linux)
    BACKEND="-DSOKOL_GLCORE"
    # sokol_app: X11, Xi, Xcursor, dl, pthread, m for every backend; GL for
    # SOKOL_GLCORE. -pthread is needed at compile and link time both.
    LIBS="-lGL -lX11 -lXi -lXcursor -ldl -lm -pthread"
    # sokol calls clock_gettime, which is POSIX rather than ISO C, so
    # -std=c11 hides its declaration. The feature macro goes on sokol's
    # translation unit alone: hologram's own sources stay strict ISO,
    # which is what caught a missing stdlib.h the first time this ran.
    DISPLAY_CFLAGS="-D_POSIX_C_SOURCE=200809L"
    ;;
Darwin)
    BACKEND="-DSOKOL_METAL"
    LIBS="-framework AppKit -framework QuartzCore -framework Metal -framework MetalKit"
    # sokol's implementation must be compiled as Objective-C on macOS. The
    # flag goes on that one translation unit rather than renaming the file,
    # so display.c stays the only file that talks to sokol.
    DISPLAY_CFLAGS="-x objective-c -fobjc-arc"
    ;;
*)
    echo "build.sh: unsupported platform '$UNAME' -- Linux and macOS only" >&2
    exit 1
    ;;
esac

mkdir -p build

if [ "${1:-}" = "test" ]; then
    failed=0
    for t in tests/test_*.c; do
        name=$(basename "$t" .c)
        # shellcheck disable=SC2086
        $CC $CFLAGS $WARN -Isource -Itests -o "build/$name" "$t" $PURE -lm
        if ! "./build/$name"; then
            failed=1
        fi
    done
    exit $failed
fi

# sokol's own warnings are not ours to fix, so its implementation TU builds
# without -Wall -Wextra -- the same split build.bat makes with /W3 vs /W4.
# shellcheck disable=SC2086
$CC $CFLAGS $BACKEND $DISPLAY_CFLAGS -c source/display.c -o build/display.o

build_example() {
    name=$1
    shift
    # shellcheck disable=SC2086
    $CC $CFLAGS $WARN $BACKEND -o "build/$name" "examples/$name/main.c" \
        build/display.o "$@" $PURE $LIBS
}

# m1_cpu is the CPU tracer alone: no sokol, no display, no backend.
# shellcheck disable=SC2086
$CC $CFLAGS $WARN -o build/m1_cpu examples/m1_cpu/main.c $PURE -lm

build_example m0_window
for e in m2_gpu m3_mirrors m4_glass m5_spectral m6_polarization; do
    build_example "$e" source/oracle.c
done
for e in m7_room m8_furnace m9_spectrum; do
    build_example "$e" source/oracle.c source/input.c
done

# tools/bench: GPU cost per panel. It has a real GPU clock only on
# D3D11; elsewhere it falls back to the frame clock and says so.
# shellcheck disable=SC2086
$CC $CFLAGS $WARN $BACKEND -I. -o build/bench tools/bench/bench.c \
    build/display.o $PURE $LIBS

echo "built into build/ for $UNAME ($BACKEND)"
