#!/usr/bin/env bash
#
# mayhem/build.sh — build gl4es twice and link the fuzz harnesses and the self-test.
#
# gl4es is built as upstream documents for a static library without a window system:
#   cmake -DNOX11=ON -DNOEGL=ON -DSTATICLIB=ON -DNO_LOADER=ON -DNO_INIT_CONSTRUCTOR=ON
# The GLES backend is the fake driver in mayhem/harnesses/fake_gles.c, installed through
# include/gl4esinit.h (set_getprocaddress + initialize_gl4es).
#
#   build/fuzz  sanitized + DWARF-3 library  -> /mayhem/fuzz_* and /mayhem/fuzz_*-standalone
#   build/test  normal flags                 -> /mayhem/gl4es_selftest (run by mayhem/test.sh)
#
# Upstream's CMakeLists writes the archive into <src>/lib/libGL.a for every build dir
# (CMAKE_ARCHIVE_OUTPUT_DIRECTORY), so each build removes it first and copies it out.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
H="$SRC/mayhem/harnesses"
GL_OPTS=(-DNOX11=ON -DNOEGL=ON -DSTATICLIB=ON -DNO_LOADER=ON -DNO_INIT_CONSTRUCTOR=ON -DCMAKE_BUILD_TYPE=)
INC=(-DGL_GLEXT_PROTOTYPES -I"$SRC/include" -I"$H")
LIBS=(-lm -ldl -lpthread)

build_gl() { # <build dir> <cflags>
    rm -f "$SRC/lib/libGL.a"
    cmake -S "$SRC" -B "$1" "${GL_OPTS[@]}" -DCMAKE_C_FLAGS="$2" >/dev/null
    cmake --build "$1" -j"$MAYHEM_JOBS"
    cp "$SRC/lib/libGL.a" "$1/libGL.a"
}

# 1) the test/oracle build: normal flags
build_gl build/test "-O2 $COVERAGE_FLAGS"
$CC -O2 $COVERAGE_FLAGS "${INC[@]}" "$H/selftest.c" "$H/fake_gles.c" build/test/libGL.a "${LIBS[@]}" \
    -o /mayhem/gl4es_selftest

# 2) the fuzz build: the library itself with the sanitizers and DWARF 3.
# Two UBSan checks are relaxed because gl4es trips them on nearly every input, which would
# hide everything else (ASan and the rest of UBSan stay on and halting):
#   float-cast-overflow  getter.c converts GLuint state through float (glPushAttrib reads the
#                        0xFFFFFFFF stencil mask on every call), and pixel.c casts unclamped
#                        float pixels to integers;
#   shift-base           decompress.c builds every DXT texel as `255 << 24` in int;
#   pointer-overflow     listrl.c points immediate-mode attributes at NULL+offset into the
#                        not-yet-allocated merger buffer on every glBegin/glColor/glVertex
#                        (a real dereference of such a pointer is still an ASan SEGV).
UB_RELAX="-fno-sanitize=float-cast-overflow,shift-base,pointer-overflow"
# -fsanitize=fuzzer-no-link puts libFuzzer's coverage instrumentation into the library and
# harness objects; the engine itself is linked only into the fuzz binaries.
FUZZ_CFLAGS="-O1 $SANITIZER_FLAGS $UB_RELAX $DEBUG_FLAGS -fsanitize=fuzzer-no-link"
build_gl build/fuzz "$FUZZ_CFLAGS"

mkdir -p build/harness
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o build/harness/lsan_off.o
$CC $FUZZ_CFLAGS -c "$STANDALONE_FUZZ_MAIN" -o build/harness/standalone_main.o
# The fake driver resolves unknown GLES names to one generic no-op, so it is built without
# UBSan's function-type check (the check applies to callees carrying its signature).
$CC $FUZZ_CFLAGS -fno-sanitize=function "${INC[@]}" -c "$H/fake_gles.c" -o build/harness/fake_gles.o

for f in fuzz_shader fuzz_arbprogram fuzz_compressed_tex fuzz_teximage fuzz_raster fuzz_glstream; do
    $CC $FUZZ_CFLAGS "${INC[@]}" -c "$H/$f.c" -o "build/harness/$f.o"
    $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "build/harness/$f.o" build/harness/fake_gles.o \
        build/harness/lsan_off.o build/fuzz/libGL.a "${LIBS[@]}" -o "/mayhem/$f"
    $CXX $SANITIZER_FLAGS $DEBUG_FLAGS build/harness/standalone_main.o "build/harness/$f.o" \
        build/harness/fake_gles.o build/harness/lsan_off.o build/fuzz/libGL.a "${LIBS[@]}" \
        -o "/mayhem/$f-standalone"
done
