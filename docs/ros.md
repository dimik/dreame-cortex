# ROS 2 integration

How the robot's data reaches ROS 2, what's published, and how to consume it.

**Decided architecture (see below):** the **Q6A runs all of ROS**; the robot is a pure data-source
appliance exposing **HTTP/RTSP** (no ROS in the production path). ROS 2 **Jazzy** is also installed
in the robot's chroot (`/data/chroot`, `source /opt/ros/jazzy/setup.bash`) — currently used to run
the bridge there, which works but is the **fallback / test rig**, not production. We're ROS 2 (DDS,
no master); the robot has no vendor ROS (AVA is MIIO/serial), so there's one ROS graph and it lives
on the Q6A.

## What's live today: Valetudo → ROS bridge ✅

`scripts/robot/valetudo_bridge.py` (runs in the chroot) bridges Valetudo's HTTP API into ROS —
**no MQTT broker**. It seeds the map once via REST, then is **push-driven via SSE** (no polling).

| Topic | Type | QoS | Source | Rate |
|-------|------|-----|--------|------|
| `/map` | `nav_msgs/OccupancyGrid` | RELIABLE + **TRANSIENT_LOCAL** (latched) | Valetudo `/state/map` floor(0)/wall(100)/unknown(-1) | on map change |
| `/odom` | `nav_msgs/Odometry` | default (volatile) | `robot_position` entity (pose only; **no twist**) | 2 Hz heartbeat |
| TF `map`→`base_link` | tf2 | — | same pose | 2 Hz heartbeat |
| `/robot/status` | `std_msgs/String` | default | `StatusStateAttribute` (`value/flag`) | on attr change |

**Design (why this shape):** Valetudo's map SSE is *push-on-change with no initial snapshot*, so a
docked/just-started robot would have no map until it moved → we GET the map once to seed, then ride
the SSE. `/odom`+TF are republished by a **2 Hz in-memory heartbeat** (cached pose, no HTTP) so they
stay fresh while idle — nav2 needs continuous TF. Net: zero idle HTTP traffic, instant updates while
cleaning, continuous odom/TF always.

**Coordinate convention:** Valetudo is **mm, +y DOWN** (image coords); ROS (REP-103) is **m, +y UP**.
So `x = x_mm/1000`, `y = -y_mm/1000`, `yaw = -angle_deg`. ⚠️ The yaw sign/offset is v1 — eyeball the
`base_link` heading arrow in RViz once and flip if needed.

**Run / control (on the robot):**
```sh
sh /data/valetudo_bridge.sh start|stop|status     # auto-starts at boot via _root_postboot.sh
```

**Consume (on the robot chroot):**
```sh
source /opt/ros/jazzy/setup.bash
ros2 topic echo /odom --once
ros2 topic echo /tf --once
# /map is LATCHED — the subscriber MUST match QoS or it sees nothing:
ros2 topic echo /map --once --qos-reliability reliable --qos-durability transient_local --field info
```

## Decided architecture: robot = appliance, Q6A = all ROS

**The robot runs NO ROS in the production path.** It's a pure data-source appliance exposing
**HTTP/RTSP**; the Q6A runs the **entire** ROS 2 graph. The robot↔Q6A interface is plain HTTP/RTSP,
**not DDS** — so there's no cross-host DDS to configure, no master, no domain bridging.

```
Robot (appliance — no ROS)                 Q6A (ALL ROS, one DDS domain)
  AVA + Valetudo  (REST/SSE :80)  ──HTTP──►  valetudo_bridge.py --host http://192.168.10.1
  go2rtc          (RTSP :8554)    ──RTSP──►  camera→image node (optional)
  LD_PRELOAD shims + camstream               nav2, SLAM, vision, AI/LLM, planning
```

Why (not ROS 1 dual-masters, not on-robot ROS): we're **ROS 2 (DDS, no master)**, and the robot has
**no vendor ROS** — AVA speaks MIIO/serial, so there's nothing to bridge *between* two ROS graphs;
there's one graph (ours) and it should live on the Q6A. The Allwinner A7 is weak and `/data` eMMC
**wedged under write load** this session (sysrq reboot) → keep ROS/DDS off the robot. Running the
bridge on the robot (the current state) is the worst of both — ROS load on the robot AND still
needing cross-host DDS; relocating it to the Q6A removes both.

**Migration (when the Q6A is online):** `valetudo_bridge.py` is host-agnostic — run it on the Q6A
with `--host http://<robot-ip>`; same idea for a camera→image node off the go2rtc RTSP URL. Then
disable the robot-side bridge in `_root_postboot.sh`. The chroot-ROS bridge stays as a robot-local
test rig / fallback (not the production path).

## The raw `/scan` gap — how to proceed

Valetudo exposes the **processed map + pose, not raw laser ranges**, and AVA owns `/dev/ttyS3` (the
LDS) exclusively — so there's no clean `sensor_msgs/LaserScan` source. Decide by *why* you need it:

- **Map-based nav (localize + global plan)** → **don't need `/scan`.** nav2 works against `/map` +
  AVA's pose (published as `/odom`+TF). AVA already does the SLAM; we consume it. ← v1.
- **Dynamic obstacle avoidance** (things not in the map) → use the **camera** (already streamed via
  go2rtc) + the **Q6A NPU** as a vision obstacle layer. Leverages what we have; zero robot risk.
- **Real live 360° scan / own SLAM** → add a **dedicated USB lidar on the Q6A** (LD06/LD19/RPLiDAR,
  ~$70) → native LaserScan from its driver. Robust, standard; the robot's lidar stays AVA's.
- **Tap raw ttyS3 in AVA** → **PARKED** — same `read()`-interposition wall as the IMU tap (mcutap);
  only revisit with an AVA-safe tap mechanism. Synthetic ray-cast from the map adds nothing over
  nav2's static layer and is circular for localization — skip it.

**Recommendation:** ship v1 nav on `/map` + pose (no `/scan`); add live sensing on the Q6A side
(camera+NPU, or a cheap USB lidar) when needed. Don't reach into AVA for ttyS3.

## Parked: raw IMU / wheel-odom over ttyS4

Validated but deferred — `scripts/robot/mcutap.c` would tap AVA's MCU Status stream (IMU gyro/accel,
wheel odom, currents), but the LD_PRELOAD `read()` interposition destabilises AVA. See
`docs/sensors.md` (Microphone/IMU sections) for the full finding + the AVA-safe-tap open question.
Until then, `/odom` is Valetudo's SLAM pose (good for nav), not raw dead-reckoning, and there's no
`sensor_msgs/Imu`.

## Roadmap

- [x] `/map`, `/odom`, TF, `/robot/status` from Valetudo (broker-free, SSE-driven)
- [ ] **relocate the bridge to the Q6A** (`--host http://<robot-ip>`), then disable the robot-side
      one — no cross-host DDS needed (robot↔Q6A is HTTP/RTSP)
- [ ] nav2 bringup on the Q6A consuming `/map` + TF; goals → Valetudo REST move commands
- [ ] camera into ROS (the go2rtc RTSP/WebRTC feed → an `image` topic if wanted)
- [ ] live obstacle sensing on the Q6A when needed: camera+NPU layer, or a dedicated USB lidar
- [ ] (parked) raw `/scan` via ttyS3 tap + `sensor_msgs/Imu` via ttyS4 tap — both blocked on an
      AVA-safe read tap (see mcutap.c / docs/sensors.md)

## Gotchas

- `/map` is latched (TRANSIENT_LOCAL) — `ros2 topic echo` shows nothing unless you pass
  `--qos-durability transient_local --qos-reliability reliable`.
- The bridge is a chroot process; launch it `setsid`-detached (the launcher handles this) or it dies
  when the ssh session closes.
- It needs Valetudo's HTTP API up; at boot it retries until Valetudo answers.
