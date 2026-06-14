#!/bin/sh
# Late boot hook — runs after all Dreame services start
# Handles: WiFi DHCP, Ubuntu chroot mounts, Valetudo startup

sleep 30

# --- WiFi ---
logger -t postboot "checking WiFi status"
STATUS=$(wpa_cli -iwlan0 status 2>/dev/null | grep "^wpa_state" | cut -d= -f2)
logger -t postboot "wpa_state=$STATUS"

if [ "$STATUS" = "COMPLETED" ]; then
    udhcpc -i wlan0 -b -p /var/run/udhcpc.wlan0.pid 2>/dev/null
    logger -t postboot "DHCP started"
else
    logger -t postboot "Not connected ($STATUS) - killing hostapd and retrying"
    killall -9 hostapd 2>/dev/null; sleep 2
    ifconfig wlan0 down; sleep 1; ifconfig wlan0 up
    killall -9 wpa_supplicant 2>/dev/null; sleep 1
    mkdir -p /var/run/wpa_supplicant
    wpa_supplicant -Dnl80211 -B -s -iwlan0 -c/etc/wifi/wpa_supplicant.conf
    sleep 20
    STATUS2=$(wpa_cli -iwlan0 status 2>/dev/null | grep "^wpa_state" | cut -d= -f2)
    [ "$STATUS2" = "COMPLETED" ] && udhcpc -i wlan0 -b -p /var/run/udhcpc.wlan0.pid 2>/dev/null
fi

# --- Ubuntu chroot mounts ---
logger -t postboot "mounting chroot filesystems"
CHROOT=/data/chroot
mount -t proc proc $CHROOT/proc 2>/dev/null || true
mount -t sysfs sysfs $CHROOT/sys 2>/dev/null || true
mount --bind /dev $CHROOT/dev 2>/dev/null || true
mount --bind /dev/pts $CHROOT/dev/pts 2>/dev/null || true
cp /etc/resolv.conf $CHROOT/etc/resolv.conf 2>/dev/null || true
logger -t postboot "chroot mounts done"

# --- Valetudo ---
sleep 5
logger -t postboot "starting Valetudo"
VALETUDO_CONFIG_PATH=/data/valetudo_config/valetudo.json /data/valetudo > /tmp/valetudo.log 2>&1 &
