# WiFi setup

Configure the Dreame D10s Pro to connect to a home WiFi network on boot.

## Context
The robot uses a bind-mount trick because its init script ignores /data/config/wifi/wpa_supplicant.conf when /usr/bin/wifi_manager exists (see docs/wifi-hack.md). The fix is in scripts/robot/_root.sh.

## Steps

1. **Generate the hex PSK** for the target network:
   ```bash
   wpa_passphrase "SSID" "PASSWORD" | grep "^\s*psk=" | cut -d= -f2
   ```

2. **Update scripts/robot/_root.sh** — set the correct SSID and hex PSK in the `network={}` block. Never commit real credentials; use a local override file instead.

3. **Deploy to robot** (robot must be reachable on AP 192.168.5.1 or existing WiFi):
   ```bash
   scp scripts/robot/_root.sh scripts/robot/_root_postboot.sh root@<robot-ip>:/data/
   ssh root@<robot-ip> 'chmod +x /data/_root.sh /data/_root_postboot.sh'
   ```

4. **Test without reboot** — run the bind mount manually:
   ```bash
   ssh root@<robot-ip> 'sh /data/_root.sh'
   wpa_cli -iwlan0 status
   ```
   If wpa_state != COMPLETED, kill hostapd and retry wpa_supplicant manually.

5. **Reboot and verify**:
   ```bash
   ssh root@<robot-ip> 'reboot'
   # wait ~60s, then:
   ping 192.168.1.213   # or whatever IP DHCP assigns
   ```

## Changing networks
Edit the network={} block in _root.sh, redeploy, reboot. Only one network entry is needed — the robot has a single 2.4GHz radio and cannot roam.
