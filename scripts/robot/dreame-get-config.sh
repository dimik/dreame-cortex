#!/bin/bash

STAGE1_DIR="/home/dp/Downloads/dust-fel-mr813"
FSBL="$STAGE1_DIR/fsbl_ddr3.bin"
PAYLOAD="$STAGE1_DIR/payload.bin"

echo "==> Checking for FEL device..."
if ! sunxi-fel ver; then
    echo "ERROR: Robot not detected in FEL mode. Connect USB and retry."
    exit 1
fi

echo ""
echo "==> Writing FSBL..."
sunxi-fel write 0x28000 "$FSBL" || { echo "ERROR: sunxi-fel write fsbl failed"; exit 1; }

echo "==> Executing FSBL..."
sunxi-fel exe 0x28000 || { echo "ERROR: sunxi-fel exe fsbl failed"; exit 1; }

echo "==> Waiting 5s for DDR init..."
sleep 5

echo "==> Writing payload..."
sunxi-fel write 0x4a000000 "$PAYLOAD" || { echo "ERROR: sunxi-fel write payload failed"; exit 1; }

echo "==> Executing payload (entering fastboot mode)..."
sunxi-fel exe 0x4a000000 || { echo "ERROR: sunxi-fel exe payload failed"; exit 1; }

echo "==> Waiting 3s for fastboot to come up..."
sleep 3

echo ""
echo "==> Getting config value..."
CONFIG=$(fastboot getvar config 2>&1)
echo "$CONFIG"
echo ""

VALUE=$(echo "$CONFIG" | grep -i "config:" | awk '{print $2}')
if [ -n "$VALUE" ]; then
    echo "========================================="
    echo "YOUR CONFIG VALUE: $VALUE"
    echo "========================================="
else
    echo "WARNING: Could not parse config value from output above."
fi
