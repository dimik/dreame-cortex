#!/usr/bin/env python3
"""audio_bridge.py — robot speaker bridge: play audio files AND speak text, via Dreame's mediad.

Subscribes /robot/speak (std_msgs/String); the message is one of:
  - text                          -> TTS (espeak-ng) -> spoken on the robot
  - an existing .ogg file path     -> played as-is
  - "stop"                         -> stops playback

Everything plays through Dreame's media daemon (mda_cli wire protocol over TCP 127.0.0.1:10100),
NOT aplay/ogg123 directly — so it's serialized with AVA's own prompts and never fights the ALSA
codec. The chroot reaches :10100 directly (shares host net). TTS + OGG encode are self-contained in
the chroot: espeak-ng -> /opt/ffmpeg (libvorbis) -> /tmp/*.ogg. See docs/audio.md.

Voice/speed/amplitude/volume are ROS params (see __init__) — change the voice with any espeak-ng
variant, e.g. en-us, en-us+f3, en-gb, en-us+m3, en-us+whisper.

Run: source /opt/ros/jazzy/setup.bash && python3 audio_bridge.py
     ros2 topic pub --once /robot/speak std_msgs/msg/String "{data: 'Docking complete'}"
"""
import os
import socket
import subprocess

import rclpy
from rclpy.node import Node
from std_msgs.msg import String

MEDIAD = ('127.0.0.1', 10100)
FFMPEG = '/opt/ffmpeg'
TTS_WAV = '/tmp/_spoke.wav'
TTS_OGG = '/tmp/_spoke.ogg'


class AudioBridge(Node):
    def __init__(self):
        super().__init__('audio_bridge')
        self.declare_parameter('volume', 90)        # mediad volume (board-scaled)
        self.declare_parameter('voice', 'en-us+f3')  # any espeak-ng voice/variant
        self.declare_parameter('speed', 155)         # espeak words/min
        self.declare_parameter('amplitude', 200)     # espeak amplitude 0-200
        self.vol = int(self.get_parameter('volume').value)
        self.voice = self.get_parameter('voice').value
        self.speed = int(self.get_parameter('speed').value)
        self.amp = int(self.get_parameter('amplitude').value)
        self.create_subscription(String, '/robot/speak', self.on_speak, 10)
        self.get_logger().info(f'audio_bridge up; /robot/speak = text (TTS, voice={self.voice}) '
                               f'| <path>.ogg | "stop"')

    def send_mediad(self, msg):
        try:
            s = socket.socket(); s.settimeout(2.0)
            s.connect(MEDIAD); s.sendall(msg.encode()); s.close()
            return True
        except Exception as e:
            self.get_logger().warn(f'mediad send failed: {e}')
            return False

    def play_ogg(self, path):
        if self.send_mediad(f'single,{path[:-4]},{self.vol}'):   # mediad appends .ogg
            self.get_logger().info(f'playing {path} @vol {self.vol}')

    def say_text(self, text):
        try:
            subprocess.run(['espeak-ng', '-v', self.voice, '-s', str(self.speed),
                            '-a', str(self.amp), '-w', TTS_WAV, text],
                           check=True, capture_output=True, timeout=30)
            subprocess.run([FFMPEG, '-hide_banner', '-loglevel', 'error', '-y',
                            '-i', TTS_WAV, '-c:a', 'libvorbis', TTS_OGG],
                           check=True, capture_output=True, timeout=30)
        except Exception as e:
            self.get_logger().warn(f'TTS failed: {e}')
            return
        if self.send_mediad(f'single,{TTS_OGG[:-4]},{self.vol}'):
            self.get_logger().info(f'speaking: {text[:60]!r}')

    def on_speak(self, m):
        text = m.data.strip()
        if not text:
            return
        if text == 'stop':
            subprocess.run(['killall', 'ogg123'], capture_output=True)
            return
        if text.endswith('.ogg') and os.path.exists(text):
            self.play_ogg(text)
        else:
            self.say_text(text)


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
