#!/bin/sh
# Enters the Ubuntu 24.04 arm64 chroot at /data/chroot.
# Mounts are also set up by _root_postboot.sh on boot,
# but this script can re-mount them if run manually.

CHROOT=/data/chroot
mount -t proc proc $CHROOT/proc 2>/dev/null || true
mount -t sysfs sysfs $CHROOT/sys 2>/dev/null || true
mount --bind /dev $CHROOT/dev 2>/dev/null || true
mount --bind /dev/pts $CHROOT/dev/pts 2>/dev/null || true
cp /etc/resolv.conf $CHROOT/etc/resolv.conf 2>/dev/null || true
exec chroot $CHROOT /bin/bash
