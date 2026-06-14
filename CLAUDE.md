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

## Robot internals and configuration

### AVA daemon internals

AVA (`/ava/bin/ava`) is a closed-source C++ binary running as PID 1-level process on the robot. It owns all hardware:
- Motors (wheels, side brush, main brush, fan/vacuum, mop pump) via `/dev/ttyS3` (MCU serial, 230400 baud, binary `55 AA`-prefixed protocol, fd 26 of ava process)
- LiDAR via `/dev/ttyS4` (fd 24 of ava process)
- SLAM, path planning, behavior tree

AVA is structured as a behavior tree with named nodes. Nodes communicate via pub/sub (nanomsg) and expose a command interface through `/tmp/avacmd.socket`.

**Key AVA file descriptors:**
| fd | Device | Purpose |
|----|--------|---------|
| 24 | `/dev/ttyS4` | LiDAR serial |
| 26 | `/dev/ttyS3` | MCU (motors, sensors) serial |

**Key AVA config files (on `/data/`, writable):**
| File | Purpose |
|------|---------|
| `/data/config/ava/clean_parameter.json` | Persistent cleaning settings, read at boot by porphyrion (clb) node |
| `/data/config/ava/iot.flag` | Must contain `miiot` for AVA to connect to miio_agent |
| `/data/config/ava/iot_conf.json` | IoT silent mode: `{"EnableSilent":0,"BaseUrl":""}` |
| `/data/Robot_Para.yaml` | Motor calibration offsets (not on/off control) |
| `/ava/conf/setting.yaml` | Behavior tree config (read-only squashfs; bind-mount from /data/ to override) |
| `/ava/conf/r2250.conf` | AVA node configuration; `max_average_speed:300` for porphyrion |

### avacmd — AVA node command interface

`avacmd <node_name> '<json>'` sends a command to a named AVA node via `/tmp/avacmd.socket`. All responses are JSON.

**Known nodes and their commands:**

| Node | avacmd name | Useful commands |
|------|-------------|----------------|
| msg_cvt (MIIO translator) | `msg_cvt` | `{"type":"msgCvt","cmd":"get_prop","prop":"work_mode"}` → `{"value":"N","ret":"ok"}` |
| msg_cvt | `msg_cvt` | `{"type":"msgCvt","cmd":"get_prop","prop":"clean_mode"}` → `{"value":"0","ret":"ok"}` (read-only via avacmd) |
| msg_cvt | `msg_cvt` | `{"type":"msgCvt","cmd":"status_idle"}` → `{"ret":"ok"}` |
| IoT | `iot` | `{"type":"iot","notify":"open_server"}` — connect AVA to miio_agent (needed after boot on fw 1413) |
| porphyrion (BT node) | `clb` | Network connect mode: `{"type":"clb","cmd":"report_network_connect_mode","mode":N}` |

**`{}` means unrecognized command**, not success. `{"ret":"ok"}` means recognized and executed.

**Important limitations**: `avacmd msg_cvt set_prop clean_mode 1` and all variant formats return `{}` — msg_cvt only exposes get_prop for cleaning properties, not set_prop. Direct avacmd cannot change the clean mode at runtime.

**avacmd log**: `/tmp/log/ava_cmd.log` — real-time log of all avacmd calls and responses.

### MIIO protocol stack

```
Valetudo (UDP 8053) ↔ miio_client (UDP 54321, TCP 54322/54323) ↔ miio_agent (TCP 54320) ↔ AVA
```

- **miio_client TCP 54323**: provisioning helper channel — `miio_send_line` writes here. NOT for device property commands. Only handles `_internal.*` MIIO methods.
- **miio_client UDP 54321**: receives commands from Valetudo, forwards to miio_agent.
- **miio_agent TCP 54320**: AVA connects here to receive MIIO commands from cloud/Valetudo.
- Without `iot.flag = miiot`, AVA never connects to miio_agent → all commands time out.

**MIIO property mappings (siid:piid):**

| MIIO property | siid | piid | Values |
|---------------|------|------|--------|
| work_mode | 2 | 4 | see work mode table below |
| fan_speed | 4 | 4 | 0=off, low/medium/high/max |
| clean_mode | 4 | 7 | 0=sweep (fan ON), 1=mop-only (fan OFF), 2=sweep+mop |
| water_grade | ? | ? | low/medium/high |

**Work modes (`work_mode` from avacmd):**
| Value | Meaning |
|-------|---------|
| 6 | Docked, idle |
| 14 | Dock activity (auto-empty, self-cleaning, etc.) |
| 17 | Manual remote control (HighResolutionManualControlCapability) |

### Persistent cleaning configuration — `clean_parameter.json`

`/data/config/ava/clean_parameter.json` — AVA's porphyrion (clb) node reads this at boot to initialize the behavior tree blackboard. AVA continuously overwrites this file during operation (mirroring its in-memory state).

**Key fields:**

| Field | Values | Effect |
|-------|--------|--------|
| `CleanMode` | 0=sweep, 1=mop-only, 2=sweep+mop | **1 permanently disables vacuum fan** |
| `CleanMop` | 0/1 | Whether mop pad is installed |
| `CleanCarPetPress` | 0/1 | Carpet boost |
| `StreamerSwitch` | 0/3 | Streamline feature |
| `CarpetPressState` | 0/1/2 | Carpet mode sensitivity (low/med/high) |
| `SwitchSet[CleanType]` | 0/1 | Clean type (linked to CleanMode) |

**Default (sweep mode, fan ON):**
```json
{"CleanMode":0,"CleanMop":1,"CleanBreakPonitStart":0,"CleanCarPetPress":0,"CleanWashMopTime":0,"StreamerSwitch":3,"MopSwitch":0,"CustomeSwitch":0,"ChildLock":0,"CarpetPressState":2,"MopMode":0,"UploadMap":1,"YmodeSwitch":0,"MultiMapReloc":0,"SwitchSet":[{"k":"AutoDry","v":1},{"k":"CleanType","v":0},{"k":"FillinLight","v":1},{"k":"LessColl","v":1},{"k":"MopScalable","v":1},{"k":"StainIdentify","v":1}]}
```

**Mop-only (fan OFF permanently):**
```json
{"CleanMode":1,"CleanMop":1,"CleanBreakPonitStart":0,"CleanCarPetPress":0,"CleanWashMopTime":0,"StreamerSwitch":3,"MopSwitch":0,"CustomeSwitch":0,"ChildLock":0,"CarpetPressState":2,"MopMode":0,"UploadMap":1,"YmodeSwitch":0,"MultiMapReloc":0,"SwitchSet":[{"k":"AutoDry","v":1},{"k":"CleanType","v":1},{"k":"FillinLight","v":1},{"k":"LessColl","v":1},{"k":"MopScalable","v":1},{"k":"StainIdentify","v":1}]}
```

### Permanent vacuum fan disable

**Goal**: Use `HighResolutionManualControlCapability` (manual driving) without the vacuum fan running.

**Mechanism**: Setting `CleanMode:1` (mop-only) activates the `only_mop` blackboard variable in AVA's behavior tree, which causes `mop_mode_fan_handle` to set fan=0 for all operations including manual control.

**Method** (implemented in `_root.sh`):

`_root.sh` runs **before AVA starts**, so its writes to `clean_parameter.json` are read by AVA at initialization — no race condition:

```sh
if [ -f /data/config/ava/clean_parameter.json ]; then
    sed -i 's/"CleanMode":[0-9]/"CleanMode":1/' /data/config/ava/clean_parameter.json
    logger -t root_sh "clean_parameter.json patched to CleanMode:1 (mop-only)"
fi
```

This runs on every boot, so even if AVA writes CleanMode:0 back before shutdown, the next boot patches it again before AVA reads it.

**Note on manual control speed**: CleanMode:1 (mop-only) does NOT reduce manual driving speed. Manual control wheel speed is governed by `spdv`/`spdw` MIIO parameters (`remote_params` blackboard), not by mop speed limits.

**What doesn't work**:
- `avacmd msg_cvt '{"type":"msgCvt","cmd":"set_prop","prop":"clean_mode","value":"1"}'` → `{}` (not supported)
- `avacmd clb '{"type":"clb","cmd":"set_clean_mode","value":1}'` → `{}` (not exposed)
- `miio_send_line '{"method":"set_properties","params":[{"siid":4,"piid":7,"value":1}]}'` → only sends to provisioning helper port (54323), not device command channel
- `chattr +i clean_parameter.json` → ext4 on `/data/` does not support immutable flag on kernel 4.9.191

### Valetudo REST API capabilities

Base URL: `http://192.168.1.213` (or `http://localhost` from on-robot)

**Robot state**: `GET /api/v2/robot/state/attributes` — returns array of attribute objects (fan_speed, mop, water_grade, etc.)

**Available capabilities** (`GET /api/v2/robot/capabilities`):

| Capability | Endpoint | GET returns | PUT body |
|------------|----------|-------------|----------|
| BasicControlCapability | `/api/v2/robot/capabilities/BasicControlCapability` | — | `{"command":"start"/"stop"/"pause"/"home"}` |
| FanSpeedControlCapability | `/api/v2/robot/capabilities/FanSpeedControlCapability/preset` | `{"value":"low"}` | `{"name":"low"/"medium"/"high"/"max"}` → MIIO siid:4 piid:4 |
| WaterUsageControlCapability | `/api/v2/robot/capabilities/WaterUsageControlCapability/preset` | `{"value":"low"}` | `{"name":"low"/"medium"/"high"}` |
| HighResolutionManualControlCapability | `/api/v2/robot/capabilities/HighResolutionManualControlCapability` | — | `{"operation":"enable"/"disable"/"move","velocity":N,"angle":N}` |
| QuirksCapability | `/api/v2/robot/capabilities/QuirksCapability` | quirk array | PUT `{"id":"...","value":"low"/"medium"/"high"}` — only exposes Carpet Mode Sensitivity |
| CarpetModeControlCapability | `/api/v2/robot/capabilities/CarpetModeControlCapability` | `{"enabled":false}` | `{"enabled":true/false}` |
| CleanRouteControlCapability | `/api/v2/robot/capabilities/CleanRouteControlCapability` | `{"route":"normal"}` | `{"route":"normal"/"..."}` |
| LocateCapability | `/api/v2/robot/capabilities/LocateCapability` | — | PUT (no body) — robot beeps |
| KeyLockCapability | `/api/v2/robot/capabilities/KeyLockCapability` | `{"enabled":false}` | `{"enabled":true/false}` |
| DoNotDisturbCapability | `/api/v2/robot/capabilities/DoNotDisturbCapability` | DND schedule | schedule object |
| MapSegmentationCapability | `/api/v2/robot/capabilities/MapSegmentationCapability` | — | segment clean commands |
| ZoneCleaningCapability | `/api/v2/robot/capabilities/ZoneCleaningCapability` | — | zone clean commands |
| GoToLocationCapability | `/api/v2/robot/capabilities/GoToLocationCapability` | — | `{"coordinates":{"x":N,"y":N}}` |
| SpeakerVolumeControlCapability | `/api/v2/robot/capabilities/SpeakerVolumeControlCapability` | `{"value":N}` | `{"value":0-100}` |
| ConsumableMonitoringCapability | `/api/v2/robot/capabilities/ConsumableMonitoringCapability` | consumable stats | — |
| TotalStatisticsCapability | `/api/v2/robot/capabilities/TotalStatisticsCapability` | all-time stats | — |

**No clean mode (sweep/mop-only/sweep+mop) capability is exposed by Valetudo** for this robot — controlled only via `clean_parameter.json` (see above).

### HighResolutionManualControlCapability (manual driving)

Enables joystick-style manual control. Sends MIIO property `{"siid":X,"piid":15,"value":{"spdv":N,"spdw":N,"audio":"true","random":N}}`. Puts AVA into work_mode 17.

**Enabling from REST:**
```bash
curl -X PUT http://192.168.1.213/api/v2/robot/capabilities/HighResolutionManualControlCapability \
  -H 'Content-Type: application/json' \
  -d '{"operation":"enable"}'
```

**Moving:**
```bash
curl -X PUT http://192.168.1.213/api/v2/robot/capabilities/HighResolutionManualControlCapability \
  -H 'Content-Type: application/json' \
  -d '{"operation":"move","velocity":N,"angle":N}'
```

`velocity`: forward/backward speed (positive=forward), `angle`: turning rate (positive=left).

**Warning**: Without CleanMode:1 (mop-only), enabling manual control also starts the vacuum fan.

### MCU serial communication (ttyS3)

AVA communicates with the MCU (motor controller) via `/dev/ttyS3` at 230400 baud. Protocol: binary, `55 AA`-prefixed framing. AVA holds fd 26 open to this device.

**MCU pub/sub message types** (internal to AVA, not directly accessible externally):
- `CLEANSET` — sets motor speeds (fan, brush, pump)
- `MOVE_CTRL` — sets wheel velocity
- `BUTTONLED` — controls LEDs
- `ROBOTMODE` — sets robot operating mode

Direct MCU serial access would conflict with AVA. Use avacmd/Valetudo API instead.

### avaexec socket

`/tmp/avaexec.socket` — nanomsg IPC socket. `exec_proc` node listens and executes shell commands via `system()`. Used by `msg_cvt.sh` for network/provisioning tasks. Not directly useful for cleaning mode control.

`msg_cvt.sh` at `/ava/script/msg_cvt.sh` handles: start_up, location, play, net_error, set_device_time, iot_state, iot_restore, iot_reset, download_file, reset_device, add_ap, del_ap, get_ap.

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
