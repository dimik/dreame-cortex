# Robot status check

Run a full status check on the Dreame D10s Pro. SSH to the robot at 192.168.1.213 (home network) using `ssh dreame-home`. Check and report:

1. **WiFi**: `wpa_cli -iwlan0 status` — show wpa_state and IP address
2. **Valetudo**: is the process running? tail /tmp/valetudo.log for last 10 lines
3. **AVA daemon**: is `ava` process running?
4. **Boot hooks**: confirm /data/_root.sh and /data/_root_postboot.sh exist and are executable
5. **Chroot mounts**: `mount | grep chroot` — are proc/sys/dev mounted into /data/chroot?
6. **Disk space**: `df -h /data`
7. **Uptime and load**: `uptime`

Present results as a clear status table with OK / WARN / ERROR for each item.
