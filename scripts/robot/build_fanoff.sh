#!/bin/sh
# build_fanoff.sh — compile the LD_PRELOAD fan-off shim inside the robot's Ubuntu chroot.
#
# Must build in the chroot (native aarch64 gcc-13), but FREESTANDING so the result has no
# glibc symbol deps — AVA runs glibc 2.23, the chroot is glibc 2.39. See fanoff_shim.c.
#
# Produces (in /data/lib/ on the robot):
#   libfanoff_log.so     — Phase-1 capture (passthrough + dump 55 AA frames to /tmp/mcu_tx.log)
#   libfanoff_filter.so  — production (rewrite CLEANSET fan byte -> 0)
#
# Run ON the robot:  sh /data/build_fanoff.sh
# (deploy this script + fanoff_shim.c to /data/ first; see deploy_fanoff.sh)

set -e

SRC=/data/fanoff_shim.c
OUTDIR=/data/lib
CHROOT=/data/chroot

mkdir -p "$OUTDIR"
cp "$SRC" "$CHROOT/tmp/fanoff_shim.c"

CFLAGS="-O2 -fPIC -shared -nostdlib -ffreestanding -fno-stack-protector -fno-builtin -Wall"

echo "Building libfanoff_log.so (capture mode)..."
chroot "$CHROOT" /usr/bin/gcc-13 $CFLAGS -DMODE_LOG    -o /tmp/libfanoff_log.so    /tmp/fanoff_shim.c

echo "Building libfanoff_filter.so (production mode)..."
chroot "$CHROOT" /usr/bin/gcc-13 $CFLAGS -DMODE_FILTER -o /tmp/libfanoff_filter.so /tmp/fanoff_shim.c

cp "$CHROOT/tmp/libfanoff_log.so"    "$OUTDIR/libfanoff_log.so"
cp "$CHROOT/tmp/libfanoff_filter.so" "$OUTDIR/libfanoff_filter.so"

echo "Built:"
ls -la "$OUTDIR/libfanoff_log.so" "$OUTDIR/libfanoff_filter.so"

# Sanity: confirm NO glibc version deps (must be loadable under glibc 2.23).
# readelf is in the chroot; .so should have no GLIBC_2.x verneed entries.
echo "--- version deps (should be EMPTY) ---"
chroot "$CHROOT" /usr/bin/readelf -V /tmp/libfanoff_filter.so 2>/dev/null | grep -i "GLIBC" || echo "  none — good (freestanding)"
echo "--- exported symbols (expect: write, writev) ---"
chroot "$CHROOT" /usr/bin/readelf -s /tmp/libfanoff_filter.so 2>/dev/null | grep -iE " write$| writev$" || true
