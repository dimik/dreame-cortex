# Dreame D10s Pro (r2250) — sensors & data access

What the robot can sense, where each stream lives, and how to read it for the AI-rover build.
All verified live on 2026-06-15/16.

| Sensor | Device / source | Owner | Independently readable? |
|--------|-----------------|-------|--------------------------|
| LiDAR (LDS) | `/dev/ttyS3` @ 230400 | AVA (`node_lds.so`) | via Valetudo map/MQTT, or tap ttyS3 |
| Camera (OV8856) | `/dev/video0`+`/dev/video2` (sunxi-vin) | AVA (pipeline bound to video2) | not live (sensor exclusive); JPEGs saved by AVA — see below |
| IMU + odometry + currents | MCU stream on `/dev/ttyS4` | AVA (`node_signal.so`) | yes — tap ttyS4 (strace/shim) or decode |
| Microphone | — | — | none (codec capture errors out) |

---

## LiDAR turret (LDS)

A 360° spinning laser distance sensor on **`/dev/ttyS3` @ 230400 baud**, read by `node_lds.so`.
Frame format is **`55 aa` + 36 data bytes** (38 B total) — angle + distance points per rotation.
This is a *different* protocol from the MCU link and is what older notes mislabeled as the
"MCU 55 AA protocol".

The **two holes** in the turret are the laser **emitter (TX)** window and the **receiver/photodiode
(RX)** lens — standard triangulation-LiDAR layout (project a dot, read its offset reflection,
distance = triangulation).

Read path for the rover: AVA owns ttyS3 for SLAM, so consume the processed scan/map/pose via
the **Valetudo MQTT bridge** (`/scan`, `/map`, `/odom`) rather than tapping the raw serial.

---

## Camera

- **One physical sensor: OmniVision `ov8856_mipi`** (~8 MP MIPI), via the Allwinner **sunxi-vin**
  pipeline (`sunxi_csi` → `sunxi_mipi` → `sunxi_isp` → `sunxi_scaler` → video node).
- Exposed as **two V4L2 nodes**: `/dev/video0` (`vin_video0`) and `/dev/video2` (`vin_video2`)
  — same sensor, two VIN paths.
- **AVA holds `/dev/video2` open** (plus `media0`, `isp`, `h3a` subdevs) and runs object AI on it:
  loaded nodes `node_camera_ai.so`, `node_camera_streamer.so`, `node_cameracalibr_ai.so`,
  `node_ai_area_recognition.so`. So the camera is **powered/in use by default** while AVA runs.
- The Z10's separate IR-cam + line-laser on `/dev/ttyS2` (`node_camera_laser.so`) is **NOT used**
  on this D10s — that node isn't loaded and ttyS2 is idle.

### Direct capture from /dev/video0 — blocked at runtime
`sunxi-vin` is a **multi-plane** V4L2 device (`V4L2_CAP_VIDEO_CAPTURE_MPLANE`). `/dev/video0` is
free to `open()` and enumerates a long format list (NV12/NV21/YUYV, raw Bayer BA81/RGGB/…, RGB),
but it comes up **unconfigured** (`G_FMT` → `0x0`, `num_planes=0`) and `S_FMT` returns `EINVAL`:
the media-controller links (sensor→csi→isp→scaler→video0) are not set up, and there's only one
sensor — which AVA has bound to the `video2` pipeline. Independent live capture would require
`media-ctl` (not installed on the robot) to wire the pipeline **and** the sensor freed from AVA.
The capture helper `scripts/robot/v4l2grab.c` (multi-plane V4L2 → PPM) is committed for if/when
we set that up (install `v4l-utils`+`media-ctl` in the chroot and pause AVA's camera).

### Easy frame source — AVA's AI image dumps  ✅
AVA continuously saves obstacle/furniture detection frames as JPEG to
**`/data/ai_offline_collection/*.jpg`** — real OV8856 frames, **672×504, color baseline JPEG**
(the AI-pipeline output size; sensor native is higher). Grab the latest without disturbing AVA:
```sh
ssh dreame-home 'cd /data/ai_offline_collection; ls -t *.jpg | head -1'   # newest
# pull (BusyBox has no sftp): base64 over ssh
ssh dreame-home 'base64 /data/ai_offline_collection/<file>.jpg' | base64 -d > frame.jpg
```
Sample frames pulled to `~/dreame-camera/` show the robot's low, wide-FOV view of the room.

### Live camera control — `avacmd streamer` (works, but FRAGILE — caused a reboot)
AVA's camera streamer (`node_camera_streamer.so`) is reachable on the avacmd socket as node
**`streamer`**:
- `avacmd streamer '{"cmd":"get_camera_state"}'` → `{"state":"open"}` (camera is open whenever AVA runs). **Read-only, safe.**
- Commands: `open_camera`, `close_camera`, `get_camera_state`. Params: `width`, `height`, `fps_in`,
  `fps_out`, `enable_photo_thread`, `enable_sync`. With `enable_photo_thread` it is meant to save
  JPG/BMP/YUV to `/data/www/data/camera_bmp/camera_*_width=W_heig=H.*`.
- Frame transport: `/tmp/videomonitor.socket` (the app's live-view feed).
- **⚠️ DO NOT reconfigure the live camera.** Sending `open_camera` with width/height/fps on the
  running system **crashed AVA and forced a full reboot (2026-06-16)** — and `enable_photo_thread`
  never produced files while idle (frames only flow when AVA is actively pulling them during
  navigation). AVA owns the single sensor and won't tolerate external reconfiguration.

**Safe ways to get camera frames:**
1. **AI JPEG dumps** (above) — read-only, real frames, populate during navigation. Best zero-risk option.
2. **Passive read** of `/tmp/videomonitor.socket` (connect & read only, never `open_camera`) — untested; may be empty while idle.
3. **Dedicated camera on the Q6A** (it has 3× MIPI CSI) — *recommended for the rover*: full control, no
   contention with AVA, better placement. The robot's OV8856 stays with AVA for its obstacle avoidance.

`scripts/robot/camera_stream.sh` (GStreamer off `/dev/video0`) and `scripts/robot/v4l2grab.c` remain
for if we ever set up an independent `/dev/video0` pipeline (`media-ctl` + `v4l-utils`/`gst`, sensor
freed) — but that contends with AVA for the sensor and risks the same instability.

---

## Microphone — none

The ALSA codec (`SUNXI-CODEC`, `hw:0,0`) advertises a capture channel (`capture 1`), but recording
from it returns `read error: Input/output error` and an empty file — **no microphone is wired/routed**.
The robot is speaker-out only (voice prompts). Speaker mute: `PUT SpeakerVolumeControlCapability
{"action":"set_volume","value":0}`.

---

## IMU, odometry & motion — rich, readable

Everything streams from the MCU → SoC on **`/dev/ttyS4`** (`3c..3e` framed, Modbus CRC16 — see
`../CLAUDE.md` "MCU & LDS serial protocol"). Read it read-only with `strace` on AVA, decode with
`~/dreame_mcu_protocol/mcu_packets.py`, or forward a copy from the `fanoff_shim`. Packet set:

| Packet (type) | Rate | Data |
|---|---|---|
| `Status10ms` (0x02) | 100 Hz | 3-axis **gyro** (°/s), 3-axis **accel**, L/R wheel-distance delta |
| `Status20ms` (0x01) | 50 Hz | **odometry** x, y, yaw + L/R wheel velocity + roller/side-brush current |
| `Status100ms` (0x03) | 10 Hz | **pitch / roll**, wheel current, dust/water/HEPA/carpet present bits |
| `Triggers` (0x00) | event | bumpers, cliff/IR, dock contact, per-motor over-current + error bits |
| `BatteryStatus` (0x2b) | — | voltage, current, temperature, charge, SoC % |

Live-decoded `Status10ms` frame (robot stationary, sitting flat):
```
gyro  raw x=-4 y=-18 z=16        -> ~0 (not rotating)
accel raw x=-243 y=38 z=16411    -> accel_z 16411/16384 = 1.002 g (gravity on Z, upright)
wheel dist delta L=0 R=0         -> not moving
```
(Status20ms/100ms byte layout differs slightly from the Z10 reference; gyro/accel scaling needs a
calibration pass, but structure + values are confirmed sane.)

IMU/compass ICs per the Z10 reference (D10s may differ): gyro XV7001, IMU BMI055, compass QMCX983.

---

## Recommended data plumbing for the rover (Radxa Q6A)

- **LiDAR scan + map + pose** → Valetudo **MQTT** → ROS `/scan`, `/map`, `/odom`.
- **Camera** → for now, poll `/data/ai_offline_collection/*.jpg`; longer-term, wire a real
  `/dev/video0` stream (needs `media-ctl` + `gst`/`ffmpeg` installed, sensor freed).
- **IMU / odometry / currents / triggers / battery** → tap the MCU stream on `ttyS4`
  (read-only strace or a small forwarder), decode with `mcu_packets.py`.
- **Mic** → not available.
