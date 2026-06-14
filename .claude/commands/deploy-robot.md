# Deploy robot scripts

Deploy updated boot hooks and daemons from this repo to the Dreame D10s Pro.

## Pre-flight checks
1. Confirm robot is reachable: `ping 192.168.1.213`
2. Confirm SSH works: `ssh dreame-home 'uptime'`
3. Confirm /data has enough space: `ssh dreame-home 'df -h /data'` — need at least 50MB free

## Deploy

Copy all robot-side scripts:
```bash
scp scripts/robot/_root.sh          root@192.168.1.213:/data/_root.sh
scp scripts/robot/_root_postboot.sh root@192.168.1.213:/data/_root_postboot.sh
scp scripts/robot/chroot.sh         root@192.168.1.213:/data/chroot.sh
scp scripts/robot/camera_stream.sh  root@192.168.1.213:/data/camera_stream.sh
scp scripts/robot/audio_server.py   root@192.168.1.213:/data/audio_server.py
scp robot/valetudo/valetudo.json    root@192.168.1.213:/data/valetudo_config/valetudo.json

ssh root@192.168.1.213 'chmod +x /data/_root.sh /data/_root_postboot.sh /data/chroot.sh /data/camera_stream.sh'
```

## Verify after deploy
- Check scripts are executable: `ssh dreame-home 'ls -la /data/_root*.sh'`
- Validate valetudo config: `ssh dreame-home 'cat /data/valetudo_config/valetudo.json'`
- Optionally reboot to test full boot sequence: `ssh dreame-home 'reboot'`

## Important
- Never overwrite /data/_root.sh with a version missing the SSID/PSK — robot will lose WiFi on next reboot
- If you change valetudo.json, Valetudo must be restarted: `ssh dreame-home 'killall valetudo; sleep 2; VALETUDO_CONFIG_PATH=/data/valetudo_config/valetudo.json /data/valetudo > /tmp/valetudo.log 2>&1 &'`
