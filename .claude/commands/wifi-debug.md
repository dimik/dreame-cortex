# WiFi debug

Debug WiFi connectivity on the Dreame D10s Pro. The robot is at 192.168.1.213 when connected, or 192.168.5.1 in AP mode.

Connect to whichever is reachable (`ssh dreame-wifi` or `ssh dreame`) and run this diagnostic sequence:

1. **wpa_supplicant state**: `wpa_cli -iwlan0 status`
2. **Is bind mount active?**: `mount | grep wpa_supplicant` — should show the /data/ config overlaid on /etc/wifi/wpa_supplicant.conf
3. **Config content**: `cat /etc/wifi/wpa_supplicant.conf` — should contain a `network={}` block with our SSID
4. **Is hostapd running?**: `ps | grep hostapd` — if running and wpa_state != COMPLETED, that's the conflict
5. **Interface state**: `ifconfig wlan0`
6. **DHCP lease**: `cat /var/run/udhcpc.wlan0.pid 2>/dev/null && ps | grep udhcpc`
7. **Boot log**: `logread | grep -E "(root_sh|postboot|wpa)"` — check if hooks ran

Diagnose the failure based on findings. Common issues:
- bind mount missing → _root.sh didn't run or failed; check logread
- config has no network block → PSK not written to /data/config/wifi/wpa_supplicant.conf
- hostapd conflict → kill it: `killall -9 hostapd`
- wpa_state=SCANNING but no IP → wrong channel or SSID mismatch; check `wpa_cli -iwlan0 scan_results`

Always restore WiFi connectivity before ending. If the robot AP is the only access, do not kill hostapd without ensuring wpa_supplicant can connect first.
