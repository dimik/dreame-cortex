#!/bin/sh
# deploy_fanoff.sh — install the production FILTER shim so AVA always preloads it.
#
# AVA is launched by /etc/rc.d/ava.sh (squashfs, read-only) and restarted by the watchdog
# (exec_monitor/monitor.sh). To make EVERY launch preload the shim, we bind-mount a patched
# copy of ava.sh that exports LD_PRELOAD before exec'ing ava. Same squashfs-override trick as
# the wpa_supplicant.conf bind-mount in _root.sh.
#
# This bind-mount MUST be established before AVA first starts, so call it from _root.sh
# (early boot). This script does it live AND restarts AVA so you can test without a reboot.
#
# PREREQUISITE: /data/lib/libfanoff_filter.so built (sh /data/build_fanoff.sh) with the
# FRAME SPEC constants in fanoff_shim.c filled in from the Phase-1 capture.
#
# Run ON the robot:  sh /data/deploy_fanoff.sh

set -e
SHIM=/data/lib/libfanoff_filter.so
PATCHED=/data/ava.sh.preload
ORIG=/etc/rc.d/ava.sh

[ -f "$SHIM" ] || { echo "ERROR: $SHIM not found — run build_fanoff.sh first"; exit 1; }

# Build a patched ava.sh: identical to the original but with LD_PRELOAD exported.
# We insert the export right after the shebang so it applies to the `ava ... &` exec lines.
{
  head -1 "$ORIG"
  echo "export LD_PRELOAD=$SHIM   # injected by deploy_fanoff.sh"
  tail -n +2 "$ORIG"
} > "$PATCHED"
chmod +x "$PATCHED"

echo "=== patched ava.sh preview (head) ==="
head -5 "$PATCHED"

# Bind-mount the patched copy over the squashfs original.
mount --bind "$PATCHED" "$ORIG"
echo "bind-mounted $PATCHED over $ORIG"

# Restart AVA so the new launch picks up LD_PRELOAD (watchdog or ava.sh will relaunch).
echo "restarting AVA..."
/etc/rc.d/ava.sh force >/dev/null 2>&1 &
sleep 8

# Verify the shim is mapped into the running AVA.
AVAPID=$(pidof ava)
echo "ava pid=$AVAPID"
if grep -q "libfanoff_filter.so" "/proc/$AVAPID/maps" 2>/dev/null; then
    echo "OK: libfanoff_filter.so is loaded into AVA."
else
    echo "WARNING: shim NOT found in AVA maps — check ava.sh patch / LD_PRELOAD path."
fi

echo
echo "TEST: enable manual nav from Valetudo and confirm the fan stays SILENT,"
echo "      and that driving + brush/pump still respond."
echo "To persist across reboots: add the same 'mount --bind $PATCHED $ORIG' to _root.sh"
echo "BEFORE app start (alongside the wpa_supplicant bind-mount)."
