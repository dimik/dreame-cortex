#!/bin/sh
# fanoff_flag.sh — LiDAR gate for the fanoff shim.
#
# The fanoff shim filters the vacuum fan UNCONDITIONALLY (off in every mode) and blocks the
# LiDAR turret BY DEFAULT, allowing it only while /tmp/lidar_allow exists. This poller creates
# that flag whenever the robot is in an active non-manual mode (so the LiDAR keeps running for
# mapping / go-to / autonomous nav) and removes it for manual_control / idle / docked / parked.
#
# Race-free for the user-visible case: manual_control AND idle both leave the flag absent, so
# entering manual navigation never flips the LiDAR from allowed->blocked mid-session — the
# turret is simply never spun up during manual nav. (At idle the LiDAR is parked by AVA anyway.)
# This daemon only governs how fast the LiDAR is re-allowed when leaving manual nav.
#
# Launched in the background from _root_postboot.sh after Valetudo starts.

ALLOW=/tmp/lidar_allow
URL=http://localhost/api/v2/robot/state/attributes

while true; do
    S=$(curl -s -m3 "$URL" 2>/dev/null)
    case "$S" in
        # manual / parked / unreachable -> block LiDAR (flag absent)
        *'"value":"manual_control"'* | *'"value":"idle"'* | *'"value":"docked"'* \
            | *'"value":"paused"'* | *'"value":"error"'* | *'"value":"sleeping"'* | "")
            rm -f "$ALLOW" ;;
        # any other (active) status: cleaning / returning / moving / mapping / ... -> allow LiDAR
        *)
            : > "$ALLOW" ;;
    esac
    sleep 1
done
