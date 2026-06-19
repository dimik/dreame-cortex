#!/usr/bin/env python3
"""audio_bridge.py — play audio on the robot speaker via Dreame's mediad (no ALSA contention).

Subscribes /robot/speak (std_msgs/String):
  - an absolute path to a readable .ogg file  -> plays it
  - "stop"                                     -> stops playback

Playback goes through Dreame's media daemon, NOT aplay/ogg123 directly: we send mda_cli's wire
message to mediad on TCP 127.0.0.1:10100 (reachable from the chroot — it shares the host network).
mediad appends ".ogg", sets volume (amixer hw:audiocodec), and runs ogg123 via mediad_script.sh,
SERIALIZED with AVA's own voice prompts — so our audio never collides with AVA on the ALSA codec.
(aplay on hw:0,0 would contend.) TTS runs on the companion: generate an OGG, scp it to the robot's
/tmp, then publish that path here. See docs/audio.md.

Run: source /opt/ros/jazzy/setup.bash && python3 audio_bridge.py
"""
import os
import socket
import subprocess

import rclpy
from rclpy.node import Node
from std_msgs.msg import String

MEDIAD = ('127.0.0.1', 10100)


class AudioBridge(Node):
    def __init__(self):
        super().__init__('audio_bridge')
        self.declare_parameter('volume', 70)    # mediad scales to the board max (31 on MR813)
        self.vol = int(self.get_parameter('volume').value)
        self.create_subscription(String, '/robot/speak', self.on_speak, 10)
        self.get_logger().info('audio_bridge up; /robot/speak = <path>.ogg to play, or "stop"')

    def send_mediad(self, msg):
        try:
            s = socket.socket(); s.settimeout(2.0)
            s.connect(MEDIAD); s.sendall(msg.encode()); s.close()
            return True
        except Exception as e:
            self.get_logger().warn(f'mediad send failed: {e}')
            return False

    def on_speak(self, m):
        text = m.data.strip()
        if text == 'stop':
            subprocess.run(['killall', 'ogg123'], capture_output=True)  # chroot shares host /proc
            return
        if not text.endswith('.ogg') or not os.path.exists(text):
            self.get_logger().warn(f'ignored (need an existing .ogg path or "stop"): {text!r}')
            return
        # mediad takes the path WITHOUT extension and appends .ogg
        if self.send_mediad(f'single,{text[:-4]},{self.vol}'):
            self.get_logger().info(f'playing {text} @vol {self.vol}')


def main():
    rclpy.init()
    node = AudioBridge()
    try:
        rclpy.spin(node)
    except (KeyboardInterrupt, SystemExit):
        pass
    finally:
        node.destroy_node()
        try:
            rclpy.shutdown()
        except Exception:
            pass


if __name__ == '__main__':
    main()
