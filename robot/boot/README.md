# Boot hooks

The Dreame firmware runs two hook scripts from `/data/` at boot via `/etc/rc.sysinit`:

| Script | When | Purpose |
|--------|------|---------|
| `_root.sh` | Early — before WiFi init | Bind-mount wpa_supplicant config |
| `_root_postboot.sh` | Late — after all services | DHCP, chroot mounts, Valetudo |

## Deploy

```bash
# Edit _root.sh and set YOUR_SSID and YOUR_HEX_PSK first
# Generate hex PSK: wpa_passphrase YOUR_SSID YOUR_PASSWORD | grep psk=

scp _root.sh _root_postboot.sh root@<robot-ip>:/data/
ssh root@<robot-ip> 'chmod +x /data/_root.sh /data/_root_postboot.sh'
```

## WiFi PSK generation

```bash
wpa_passphrase "MyNetwork" "MyPassword" | grep "^\s*psk=" | cut -d= -f2
```

Paste the 64-character hex string into `_root.sh` as `YOUR_HEX_PSK`.  
See [docs/wifi-hack.md](../../docs/wifi-hack.md) for why plain config files don't work.
