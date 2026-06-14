#!/bin/sh
# Early boot hook — runs before wifi.sh and factory_ap.sh
# Bind-mounts our wpa_supplicant config over the read-only squashfs copy
# so wpa_supplicant.sh picks up our home network even when wifi_manager exists.
# See docs/wifi-hack.md for full explanation.

sleep 1
mkdir -p /data/config/wifi

cat > /data/config/wifi/wpa_supplicant.conf << WPAEOF
ctrl_interface=/var/run/wpa_supplicant/
disable_scan_offload=1
update_config=0
wowlan_triggers=any

network={
    ssid="YOUR_SSID"
    psk=YOUR_HEX_PSK
    key_mgmt=WPA-PSK
    proto=WPA WPA2
    scan_ssid=1
}
WPAEOF

mount --bind /data/config/wifi/wpa_supplicant.conf /etc/wifi/wpa_supplicant.conf
logger -t root_sh "wpa_supplicant.conf bind mounted"

# Force mop-only mode (CleanMode:1) so AVA never spins up the vacuum fan.
# _root.sh runs before AVA starts, so this write wins the race.
if [ -f /data/config/ava/clean_parameter.json ]; then
    sed -i 's/"CleanMode":[0-9]/"CleanMode":1/' /data/config/ava/clean_parameter.json
    logger -t root_sh "clean_parameter.json patched to CleanMode:1 (mop-only)"
fi
