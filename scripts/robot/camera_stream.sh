#!/bin/sh
# Streams /dev/video0 as H.264 UDP to the companion board.
# Runs in the Ubuntu chroot or directly on the robot host.
# CPU cost: ~2% on Cortex-A7 at 640x480 15fps.
#
# Usage: COMPANION_IP=192.168.10.2 ./camera_stream.sh

COMPANION_IP=${COMPANION_IP:-192.168.10.2}
COMPANION_PORT=${COMPANION_PORT:-5600}
DEVICE=${DEVICE:-/dev/video0}
WIDTH=${WIDTH:-640}
HEIGHT=${HEIGHT:-480}
FPS=${FPS:-15}

exec gst-launch-1.0 -e \
  v4l2src device=$DEVICE \
  ! video/x-raw,width=$WIDTH,height=$HEIGHT,framerate=$FPS/1 \
  ! videoconvert \
  ! x264enc tune=zerolatency bitrate=1000 speed-preset=ultrafast \
  ! rtph264pay config-interval=1 pt=96 \
  ! udpsink host=$COMPANION_IP port=$COMPANION_PORT
