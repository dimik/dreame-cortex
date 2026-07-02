#!/bin/sh
# tts_speak.sh — run on the Radxa Q6A companion: turn text into speech on the robot's speaker.
#
# Keeps heavy TTS OFF the robot's weak Allwinner A7: synthesize here, ship a small OGG to the robot,
# and play it through Dreame's mediad (serialized with AVA's prompts, no ALSA contention).
#
#   ./tts_speak.sh "Docking started"            # uses default robot host + voice
#   ./tts_speak.sh "Battery low" dreame-wifi    # explicit robot host
#
# Pipeline:  TTS (Piper > espeak-ng fallback) -> WAV -> oggenc -> scp robot:/tmp -> trigger playback.
# Playback trigger: prefers the ROS bridge (ros2 topic pub /robot/speak) if ROS is sourced and the
# robot is on the DDS graph; otherwise falls back to `ssh ... mda_cli`. See docs/audio.md.
set -e
TEXT="$1"
ROBOT="${2:-dreame-wifi}"
VOL="${TTS_VOL:-70}"
[ -n "$TEXT" ] || { echo "usage: $0 \"text\" [robot_host]"; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
WAV="$TMP/s.wav"; OGG="$TMP/s.ogg"

if command -v piper >/dev/null 2>&1; then
    # adjust --model to your installed Piper voice
    echo "$TEXT" | piper --model "${PIPER_MODEL:-en_US-amy-medium.onnx}" --output_file "$WAV"
elif command -v espeak-ng >/dev/null 2>&1; then
    espeak-ng "$TEXT" -w "$WAV"
else
    echo "no TTS engine (install piper or espeak-ng)"; exit 1
fi

oggenc -Q "$WAV" -o "$OGG"
scp -q "$OGG" "root@$ROBOT:/tmp/speak.ogg"

# trigger playback
if command -v ros2 >/dev/null 2>&1 && ros2 topic list 2>/dev/null | grep -q '^/robot/speak$'; then
    ros2 topic pub --once -w 1 /robot/speak std_msgs/msg/String "{data: /tmp/speak.ogg}"
else
    ssh "root@$ROBOT" 'mda_cli "single,/tmp/speak,'"$VOL"'"'
fi
echo "spoke: $TEXT"
