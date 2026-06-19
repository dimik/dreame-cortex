#!/bin/sh
# valetudo_bridge.sh — start/stop/status the Valetudo->ROS 2 bridge (run ON the robot).
# Runs /opt/valetudo_bridge.py inside the chroot's ROS 2 Jazzy. Publishes /map, /odom,
# map->base_link TF, /robot/status from Valetudo's HTTP API (no MQTT broker). See the node header.
#
#   sh /data/valetudo_bridge.sh start|stop|status
CH=/data/chroot

case "${1:-start}" in
  start)
    [ -f "$CH/opt/valetudo_bridge.py" ] || { echo "ERROR: $CH/opt/valetudo_bridge.py missing"; exit 1; }
    pkill -9 -f valetudo_bridge.py 2>/dev/null; sleep 1
    # setsid-detached (survives this ssh session, like camstream/go2rtc); exec so python is the leader
    setsid chroot "$CH" bash -lc 'source /opt/ros/jazzy/setup.bash; exec python3 /opt/valetudo_bridge.py' \
        > /tmp/vbridge.log 2>&1 </dev/null &
    sleep 4
    echo "valetudo_bridge started (log: /tmp/vbridge.log)"
    tail -3 /tmp/vbridge.log 2>/dev/null
    ;;
  stop)
    pkill -9 -f valetudo_bridge.py 2>/dev/null
    echo "valetudo_bridge stopped"
    ;;
  status)
    pgrep -f valetudo_bridge.py >/dev/null && echo "running" || echo "not running"
    ;;
  *) echo "usage: $0 {start|stop|status}"; exit 1 ;;
esac
