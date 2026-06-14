# CLAUDE.md — dreame-cortex project knowledge

## Project goal

Turn a Dreame D10s Pro robot vacuum into an open AI platform:
- Cloud-free operation via Valetudo
- AI companion board (Radxa Dragon Q6A) for vision, navigation, audio
- ROS 2 Jazzy for all AI/autonomy — runs entirely on the companion board
- Robot hardware (motors, LiDAR, SLAM) stays controlled by the proprietary AVA daemon

---

## Hardware

### Robot — Dreame D10s Pro (model r2250)
- SoC: AllWinner MR813, quad-core Cortex-A7 @ 1.2GHz, aarch64
- WiFi: Realtek 8189fs — **2.4GHz only, single radio** (cannot do AP + STA on different channels simultaneously)
- LiDAR: on `/dev/ttyS4` — read exclusively by AVA, proprietary binary protocol
- Camera: OV8856 MIPI sensor, exposed as `/dev/video0` and `/dev/video2` via V4L2
- Speaker: SUNXI-CODEC ALSA device — `hw:0,0`, playback via `aplay`
- Root filesystem: **squashfs (physically read-only)** — cannot be remounted RW
- Writable partition: `/data/` (ext4, ~3.3GB total, ~2GB free after setup)
- SSH access: `root@192.168.1.213` (home WiFi), `root@192.168.5.1` (robot AP mode)
- SSH key: `~/.ssh/id_rsa_dreame`

### Companion — Radxa Dragon Q6A
- SoC: Qualcomm QCS6490
- CPU: 1× Kryo Prime @ 2.7GHz + 3× Gold @ 2.4GHz + 4× Silver @ 1.9GHz
- NPU: **12 TOPS** (Hexagon DSP, 6th-gen Qualcomm AI Engine)
- GPU: Adreno 643L (OpenCL, Vulkan)
- RAM: up to 16GB LPDDR5
- Connectivity: GbE, WiFi 6, BT 5.4, USB 3.1 + 3× USB 2.0, 3× MIPI CSI, 40-pin GPIO
- Power: 12V, 18–30W (powered from robot 14.8V battery via 12V buck converter)
- OS: Ubuntu 24.04 (Armbian) — ROS 2 Jazzy Tier 1 native
- Size: 85×56mm — fits in D10s Pro top compartment

### Physical link
Robot USB 2.0 host → USB-Ethernet adapter → Cat5e → Dragon Q6A GbE
- Robot static IP on link: `192.168.10.1`
- Dragon Q6A static IP on link: `192.168.10.2`

---

## Software stack

### On the robot (Dreame firmware)
- **AVA daemon**: closed-source binary, owns ALL hardware (motors, LiDAR SLAM, sensors, path planning)
- **`dummycloud` socket**: AVA's command/state interface, normally forwarded to Dreame cloud
- **Valetudo v2026.05.0**: intercepts `dummycloud` socket, exposes REST API (port 80) and MQTT (port 1883)
- **Init system**: BusyBox with `/etc/rc.sysinit` — two hook points in `/data/`

### Boot hooks (`/data/`)
| File | Timing | Role |
|------|--------|------|
| `_root.sh` | Early — before `wifi.sh` | Bind-mount wpa_supplicant config |
| `_root_postboot.sh` | Late — after all services | DHCP, chroot mounts, start Valetudo |

### WiFi (critical — read before touching)
The init script `wpa_supplicant.sh` checks for `/usr/bin/wifi_manager`. When it exists (it does on r2250), it reads from `/etc/wifi/wpa_supplicant.conf` — a **read-only squashfs file with no network entries** — instead of `/data/config/wifi/wpa_supplicant.conf`.

Fix: `_root.sh` writes our config to `/data/config/wifi/wpa_supplicant.conf` then does:
```sh
mount --bind /data/config/wifi/wpa_supplicant.conf /etc/wifi/wpa_supplicant.conf
```
This must run **before** `wpa_supplicant.sh`. See `docs/wifi-hack.md` for full explanation.

### Ubuntu 24.04 chroot (`/data/chroot/`)
- Full Ubuntu 24.04.4 arm64 base rootfs
- ROS 2 Jazzy installed (but NOT used — Dragon Q6A handles all ROS)
- Enter: `ssh dreame-home` then `sh /data/chroot.sh`
- apt fix required: `APT::Sandbox::User "root"` in `/etc/apt/apt.conf.d/00no-sandbox`
  (apt's `_apt` sandbox user cannot do DNS on kernel 4.9.191)

### Valetudo config
- Binary: `/data/valetudo` (37MB, aarch64)
- Config: `/data/valetudo_config/valetudo.json`
- Web UI: `http://192.168.1.213`
- Robot implementation: `DreameD10SProValetudoRobot` (auto-detected)
- MQTT: disabled by default — enable in valetudo.json for Dragon Q6A bridge

---

## Architecture

```
Dreame D10s Pro                              Radxa Dragon Q6A
────────────────────────────────             ──────────────────────────────────
AVA  ──► LiDAR / motors / SLAM
Valetudo REST (port 80)  ◄────────── REST ────── navigation commands
Valetudo MQTT (port 1883) ──── MQTT ──────────►  valetudo_bridge node
                                                    └─► /scan /odom /map

/dev/video0 ──► camera_stream.sh ─── UDP/H.264 ──► camera_node
                                                    └─► /camera/image_raw

/dev/snd (ALSA) ◄──────────────── TCP socket ───── audio_server.py
audio_server.py (port 9999)                          └─ TTS WAV data

Ubuntu chroot (idle, available)             inference_node (YOLOv8, NPU)
                                            nav2_stack
                                            behavior_node
                                            tts_node (Piper)
```

### Data flows

| Stream | Source | Protocol | Destination |
|--------|--------|----------|-------------|
| Camera | `/dev/video0` V4L2 | GStreamer UDP H.264 | Dragon Q6A camera_node |
| LiDAR/map | Valetudo MQTT | MQTT → ROS bridge | `/scan`, `/map` topics |
| Robot pose | Valetudo MQTT | MQTT → ROS bridge | `/odom` topic |
| Nav commands | Dragon Q6A Nav2 | Valetudo REST API | AVA motors via Valetudo |
| Audio | Dragon Q6A TTS | TCP socket (WAV) | aplay on robot speaker |

---

## Key constraints and gotchas

1. **WiFi is 2.4GHz only** — 5GHz networks won't be seen by the robot
2. **Single radio** — robot AP (hostapd on wlan0) and STA mode (wpa_supplicant) cannot run simultaneously on different channels. Our fix: kill hostapd at boot, use wlan0 as STA only.
3. **BusyBox wget cannot follow HTTPS redirects** — always use `curl -L` on the robot, or download on laptop and `scp`
4. **Kernel 4.9.191** — Ubuntu 24.04 glibc 2.39 mostly works, but apt's sandbox user (`_apt`) cannot do DNS — requires `APT::Sandbox::User "root"` workaround
5. **AVA owns `/dev/ttyS4`** — LiDAR cannot be read directly without conflicting with AVA. Use Valetudo MQTT for map/scan data instead.
6. **squashfs root** — any change to system files requires either a bind mount from `/data/` or a chroot. Never attempt `mount -o remount,rw /`.
7. **exec_monitor.sh** watches only the `ava` process — it does NOT restart hostapd if killed. Safe to kill hostapd permanently.
8. **firmware 1413 IoT flag** — AVA does NOT connect to `miio_agent` (TCP 54320) at boot unless `/data/config/ava/iot.flag` contains `miiot`. This flag is normally written during cloud provisioning (which we bypass via Valetudo). Without it, ALL Valetudo MIIO property/action commands time out. Fix: `_root_postboot.sh` writes `miiot` to the flag and calls `avacmd iot '{"type":"iot","notify":"open_server"}'` at boot.

---

## Useful commands

```bash
# SSH to robot
ssh dreame-home          # home network (192.168.1.213)
ssh dreame               # robot AP mode (192.168.5.1)

# Enter Ubuntu chroot on robot
ssh dreame-home 'sh /data/chroot.sh'

# Check Valetudo status
ssh dreame-home 'cat /tmp/valetudo.log | tail -20'

# Check WiFi connection
ssh dreame-home 'wpa_cli -iwlan0 status'

# View boot logs
ssh dreame-home 'logread | grep -E "(postboot|root_sh)"'

# Test camera
ssh dreame-home 'v4l2-ctl --device=/dev/video0 --info'

# Play test audio on robot
ssh dreame-home 'aplay -D hw:0,0 /usr/share/sounds/alsa/Front_Left.wav'
```

---

## Repository layout

```
scripts/
  robot/
    _root.sh               deploy to /data/_root.sh on robot
    _root_postboot.sh      deploy to /data/_root_postboot.sh on robot
    chroot.sh              deploy to /data/chroot.sh on robot
    camera_stream.sh       run in robot chroot to stream /dev/video0
    audio_server.py        run in robot chroot to serve audio playback
  companion/
    install_ros2.sh        run on Dragon Q6A to install ROS 2 Jazzy
robot/
  boot/README.md           deployment instructions for boot hooks
  valetudo/valetudo.json   Valetudo configuration
companion/
  ros2/                    ROS 2 node packages (valetudo_bridge, camera_node, etc.)
docs/
  hardware.md              wiring, power, physical setup
  wifi-hack.md             detailed explanation of the wpa_supplicant fix
```
