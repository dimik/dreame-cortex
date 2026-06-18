#!/bin/sh
# go2rtc.sh — start/stop the on-robot H.264/RTSP/WebRTC restreamer (run ON the robot).
#
# go2rtc (in the chroot) pulls camstream's MJPEG (localhost:8090) on demand, transcodes to H.264
# via static ffmpeg, and serves RTSP + WebRTC. Depends on camstream being up (camstream.sh /
# _root_postboot.sh) + /opt/go2rtc + /opt/ffmpeg + /opt/go2rtc.yaml in the chroot.
#
#   RTSP:   rtsp://<robot-ip>:8554/dreame      (VLC/ffplay/gst)
#   WebRTC: http://<robot-ip>:1984/            (browser)
#
#   sh /data/go2rtc.sh start|stop|status

CH=/data/chroot

case "${1:-start}" in
  start)
    [ -x "$CH/opt/go2rtc" ] && [ -x "$CH/opt/ffmpeg" ] || { echo "ERROR: /opt/go2rtc or /opt/ffmpeg missing in chroot"; exit 1; }
    mountpoint -q "$CH/tmp" || mount --bind /tmp "$CH/tmp"
    pkill -9 -f /opt/go2rtc 2>/dev/null; sleep 1
    setsid chroot "$CH" sh -c "/opt/go2rtc -config /opt/go2rtc.yaml" > /tmp/go2rtc.log 2>&1 </dev/null &
    sleep 2
    echo "go2rtc started — RTSP rtsp://<ip>:8554/dreame  WebRTC http://<ip>:1984/ (log: /tmp/go2rtc.log)"
    tail -3 /tmp/go2rtc.log 2>/dev/null
    ;;
  stop)
    pkill -9 -f /opt/go2rtc 2>/dev/null
    echo "go2rtc stopped"
    ;;
  status)
    pgrep -f /opt/go2rtc >/dev/null && echo "go2rtc running (RTSP :8554, WebRTC :1984)" || echo "not running"
    ;;
  *) echo "usage: $0 {start|stop|status}"; exit 1 ;;
esac
