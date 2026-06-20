# Power / battery

Why the rooted robot drained fast when idle, what the offender actually was, and the fixes.

> **TL;DR.** The robot went 100% → dead in ~12 h sitting idle (~6 W). Cause was **not** a runaway
> process, LiDAR, or video — it was that the SoC **never downclocks**: the stock firmware pins all 4
> cores at **1.416 GHz / full voltage 24/7** (`userspace` governor), so even at ~6% CPU the chip
> burns near-max power. Fix: switch the governor to **`ondemand`** (idles at 408 MHz, auto-ramps to
> 1.5 GHz under load) + stop two of our nodes busy-polling. Applied at boot in `_root_postboot.sh`.

## Investigation summary (2026-06)

Measured on the docked, idle robot (LiDAR off, no active video stream):

| Suspect | Verdict |
|---|---|
| LiDAR turret motor | **Not it** — gated off (`lidar_allow` absent, ring not growing) |
| Video / camera / go2rtc | **Not it** — `go2rtc` idle (`producers:[]`), camsiphon gate closed; transient `ffmpeg` only |
| A runaway process | **Not it** — system was only ~6–11% CPU busy (early "50% busy" reading was *my own* recursive greps + concurrent SSH polluting it) |
| **CPU pinned at max freq** | **THE offender** — `userspace` governor, all cores stuck at 1.416 GHz, never drops (hardware supports down to 408 MHz). High freq ⇒ high voltage ⇒ high static power even when idle |

Confirming test: switching to `ondemand` dropped idle freq **1.416 GHz → 408 MHz**, and it
auto-ramped to 1.512 GHz under load. AVA stayed alive and did **not** re-pin the governor.

Secondary (ours): `lds_scan_node` busy-polled an empty ring at a fixed 100 Hz (~6% CPU with the
LiDAR *off*), and `mcu_node` ran a flat 500 Hz reader loop (~4%).

## Fixes applied

1. **CPU governor → `ondemand`** (`_root_postboot.sh`, at boot):
   ```sh
   for c in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do echo ondemand > "$c"; done
   ```
   This unit is a **rover, not a vacuum** (no cleaning), so AVA's pinned-performance is unneeded.
   `ondemand` self-scales: 408 MHz idle, full 1.5 GHz the instant ROS/nav/vision loads the CPU.
   Verified AVA does not re-pin, so a one-time set at boot holds (no keeper daemon needed).
2. **Adaptive sensor polling** (committed in the nodes):
   - `lds_scan_node`: 50 Hz while LiDAR data flows, **2 Hz when the ring is dry** (was fixed 100 Hz).
     → 6% → ~0% CPU when the turret is off.
   - `mcu_node`: reader loop now backs off to ~50 ms when the ring is dry (was flat 500 Hz).
   Result: idle CPU 89% → **94%**, both nodes ~0% when idle.

## Verify
```sh
ssh dreame-wifi 'cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor'   # ondemand
ssh dreame-wifi 'for i in 1 2 3; do cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq; sleep 1; done'
# idle -> ~408000; rises under load
ssh dreame-wifi 'top -bn1 | head'    # lds_scan/mcu ~0% when idle
```

## Not yet done / ideas if more is needed
- **No fuel-gauge sysfs** is exposed (no `power_supply` `current_now`/`capacity`), so draw can't be
  read directly — we reason from CPU freq/util + the observed 12 h discharge.
- Further levers if battery is still short: investigate why **AVA itself stays ~6% active docked**
  (a stock robot deep-sleeps when docked — something in the rooted/Valetudo setup may keep it awake);
  consider freezing go2rtc/camstream when idle (minor now); WiFi power-save (`iw … power_save on`).
- The CPU governor was the dominant lever; revisit the above only if the `ondemand` win isn't enough.

Related: `docs/sensors.md` (the nodes), `docs/usb-gadget.md`, CLAUDE.md (reboot uses `sysrq-b`).
