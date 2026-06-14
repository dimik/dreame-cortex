#!/bin/bash

FEL_DIR="/home/dp/Downloads/dreame-fel"
CHECK_VALUE="18dbb75c"

echo "==> Checking for FEL device..."
if ! sunxi-fel ver; then
    echo "ERROR: Robot not detected in FEL mode. Connect USB and retry."
    exit 1
fi

echo "==> Writing FSBL..."
sunxi-fel write 0x28000 "$FEL_DIR/fsbl.bin" || { echo "ERROR: sunxi-fel write fsbl failed"; exit 1; }
echo "==> Executing FSBL..."
sunxi-fel exe 0x28000 || { echo "ERROR: sunxi-fel exe fsbl failed"; exit 1; }
echo "==> Waiting 5s for DDR init..."
sleep 5
echo "==> Writing payload..."
sunxi-fel write 0x4a000000 "$FEL_DIR/payload.bin" || { echo "ERROR: sunxi-fel write payload failed"; exit 1; }
echo "==> Executing payload (entering fastboot mode)..."
sunxi-fel exe 0x4a000000 || { echo "ERROR: sunxi-fel exe payload failed"; exit 1; }
echo "==> Waiting 3s for fastboot to come up..."
sleep 3

echo ""
echo "==> Verifying config value (must match: $CHECK_VALUE)..."
fastboot getvar config 2>&1

echo ""
echo "==> Flashing firmware (160s watchdog running!)..."
fastboot oem dust $CHECK_VALUE 2>&1 || { echo "ERROR: oem dust failed"; exit 1; }
fastboot oem prep 2>&1 || { echo "ERROR: oem prep failed"; exit 1; }
fastboot flash toc1 "$FEL_DIR/toc1.img" 2>&1 || { echo "ERROR: flash toc1 failed"; exit 1; }
fastboot flash boot1 "$FEL_DIR/boot.img" 2>&1 || { echo "ERROR: flash boot1 failed"; exit 1; }
fastboot flash rootfs1 "$FEL_DIR/rootfs.img" 2>&1 || { echo "ERROR: flash rootfs1 failed"; exit 1; }
fastboot flash boot2 "$FEL_DIR/boot.img" 2>&1 || { echo "ERROR: flash boot2 failed"; exit 1; }
fastboot flash rootfs2 "$FEL_DIR/rootfs.img" 2>&1 || { echo "ERROR: flash rootfs2 failed"; exit 1; }

echo ""
echo "==> Rebooting robot..."
fastboot reboot 2>&1

echo ""
echo "========================================="
echo "DONE! Robot is rebooting with rooted firmware."
echo "SSH: ssh -i ~/.ssh/id_rsa_dreame root@<robot-ip>"
echo "========================================="
