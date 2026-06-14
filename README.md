# dreame-cortex

Turning a Dreame robot vacuum into an AI platform — Valetudo integration, ROS 2 on companion board, computer vision, and custom navigation.

## Hardware

- **Robot**: Dreame D10s Pro (model r2250, AllWinner MR813 SoC, aarch64)
- **Companion board**: Radxa Dragon Q6A (Qualcomm QCS6490, 12 TOPS NPU, Adreno 643L GPU)
- **Firmware**: [Valetudo](https://valetudo.cloud) 2026.05.0 — cloud-free, local control

## Architecture

```
Dreame D10s Pro                          Radxa Dragon Q6A
─────────────────────────────            ────────────────────────────
AVA daemon (hardware control)            ALL ROS 2 nodes:
Valetudo (REST + MQTT) ──MQTT──────────► valetudo_bridge → /scan /odom /map
                       ◄─REST──────────── nav commands ← nav2 / behavior
/dev/video0 ──gstreamer/UDP────────────► camera_node → /image_raw
/dev/snd (ALSA speaker) ◄──socket──────── tts_node + audio_client
Ubuntu 24.04 chroot                      inference_node (YOLOv8 on NPU)
  └─ gstreamer daemon                    nav2_stack
  └─ audio_server.py                     behavior_node
```

Connection: robot USB 2.0 → USB-Ethernet adapter → Dragon Q6A GbE (dedicated link)  
Power: robot battery (14.8V) → 12V buck converter → Dragon Q6A USB-C

## Repository structure

```
robot/
  boot/           boot hooks that run on the Dreame at startup
  valetudo/       Valetudo configuration
  daemons/        lightweight daemons (camera stream, audio server)
companion/
  ros2/           ROS 2 nodes running on the Dragon Q6A
    valetudo_bridge/   MQTT→ROS + ROS→Valetudo REST
    camera_node/       receives GStreamer stream, publishes /image_raw
    audio_server/      TTS and audio playback client
  setup/          setup scripts for Debian 12 + ROS 2 Jazzy on Dragon Q6A
docs/
  hardware.md     wiring, power, physical mounting
  wifi-hack.md    why the bind-mount trick is needed for WiFi
  valetudo.md     Valetudo setup and API notes
```

## Quick start

See [docs/hardware.md](docs/hardware.md) for wiring and physical setup.  
See [robot/boot/README.md](robot/boot/README.md) for deploying boot hooks to the robot.  
See [companion/setup/](companion/setup/) for Dragon Q6A OS and ROS 2 setup.
