# Why the WiFi bind-mount hack is needed

## The problem

The Dreame D10s Pro firmware (`/etc/init.d/wpa_supplicant.sh`) has this logic:

```sh
if [ -f /usr/bin/wifi_manager ]; then
    wpa_supplicant ... -c /etc/wifi/wpa_supplicant.conf   # read-only squashfs, EMPTY
else
    wpa_supplicant ... -c /data/config/wifi/wpa_supplicant.conf  # writable
fi
```

`/usr/bin/wifi_manager` exists on the r2250 → the init script always reads from
`/etc/wifi/wpa_supplicant.conf`, which is on the read-only squashfs and contains
no network entries. Writing to `/data/config/wifi/wpa_supplicant.conf` (the
writable path) has no effect at boot.

The root filesystem is squashfs — physically read-only, cannot be remounted RW.

## The fix

`_root.sh` (early boot hook, runs before `wifi.sh`) does:

```sh
cat > /data/config/wifi/wpa_supplicant.conf << EOF
... network entry ...
EOF
mount --bind /data/config/wifi/wpa_supplicant.conf /etc/wifi/wpa_supplicant.conf
```

The bind-mount overlays our writable file over the read-only squashfs path.
When `wpa_supplicant.sh` runs seconds later, it reads `/etc/wifi/wpa_supplicant.conf`
and sees our network entry.

## Why Valetudo doesn't fix this

Valetudo explicitly does not manage WiFi:
> "WiFi connectivity is established by the robot's existing WiFi stack. Valetudo doesn't handle it."

Valetudo was designed for robots already connected to WiFi via the Dreame cloud app.
This hack is only needed when setting up without ever using the Dreame app.

## Alternatives considered

| Option | Why rejected |
|--------|-------------|
| Remove `/usr/bin/wifi_manager` | Risky — may break AVA hardware init |
| Rebuild squashfs | Essentially replacing the OS, defeats the purpose |
| Use `wifi_manager` API | Undocumented, closed-source binary |
