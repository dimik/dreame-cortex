# ROS 2 integration

How the robot's data reaches ROS 2, what's published, and how to consume it. ROS 2 **Jazzy** is
installed in the robot's Ubuntu chroot (`/data/chroot`, `source /opt/ros/jazzy/setup.bash`); the
Radxa Dragon Q6A is the intended main ROS host (nav/vision), subscribing over the GbE link.

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

## Consuming from the Dragon Q6A (cross-host DDS) — TODO

The bridge publishes on the robot's local ROS graph. For the Q6A to subscribe over the GbE link
(robot `192.168.10.1` ↔ Q6A `192.168.10.2`):
- same `ROS_DOMAIN_ID` on both;
- DDS discovery must reach across the link — either rely on multicast (if the USB-Eth link passes
  it) or use a Fast-DDS / Cyclone unicast peers list / discovery server pointing at the robot IP;
- `ROS_LOCALHOST_ONLY=0`.
Not yet set up — the publishing side is done; this is the next ROS task.

## Parked: raw IMU / wheel-odom over ttyS4

Validated but deferred — `scripts/robot/mcutap.c` would tap AVA's MCU Status stream (IMU gyro/accel,
wheel odom, currents), but the LD_PRELOAD `read()` interposition destabilises AVA. See
`docs/sensors.md` (Microphone/IMU sections) for the full finding + the AVA-safe-tap open question.
Until then, `/odom` is Valetudo's SLAM pose (good for nav), not raw dead-reckoning, and there's no
`sensor_msgs/Imu`.

## Roadmap

- [x] `/map`, `/odom`, TF, `/robot/status` from Valetudo (broker-free, SSE-driven)
- [ ] cross-host DDS so the Q6A subscribes
- [ ] `/scan` (Valetudo doesn't expose raw LiDAR; derive from map, or revisit)
- [ ] nav2 bringup on the Q6A consuming `/map` + TF; goals → Valetudo REST move commands
- [ ] camera into ROS (the go2rtc RTSP/WebRTC feed → an `image` topic if wanted)
- [ ] `sensor_msgs/Imu` once an AVA-safe ttyS4 tap exists (see mcutap.c)

## Gotchas

- `/map` is latched (TRANSIENT_LOCAL) — `ros2 topic echo` shows nothing unless you pass
  `--qos-durability transient_local --qos-reliability reliable`.
- The bridge is a chroot process; launch it `setsid`-detached (the launcher handles this) or it dies
  when the ssh session closes.
- It needs Valetudo's HTTP API up; at boot it retries until Valetudo answers.
