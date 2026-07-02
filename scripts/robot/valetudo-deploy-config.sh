#!/bin/bash
# Deploy Valetudo config from repo to robot and restart Valetudo.
# Usage: ./valetudo-deploy-config.sh [robot-host]
#   robot-host defaults to dreame-wifi (192.168.1.213 via ~/.ssh/config)

set -e

ROBOT="${1:-dreame-wifi}"
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CONFIG_SRC="$REPO_ROOT/robot/valetudo/valetudo.json"
CONFIG_DST="/data/valetudo_config/valetudo.json"

if [ ! -f "$CONFIG_SRC" ]; then
    echo "ERROR: config not found at $CONFIG_SRC"
    exit 1
fi

echo "==> Copying config to $ROBOT:$CONFIG_DST ..."
ssh "$ROBOT" "cat > ${CONFIG_DST}.tmp" < "$CONFIG_SRC"

# Validate it landed non-empty before replacing
SIZE=$(ssh "$ROBOT" "wc -c < ${CONFIG_DST}.tmp")
if [ "$SIZE" -lt 10 ]; then
    echo "ERROR: transfer produced empty file, aborting"
    ssh "$ROBOT" "rm -f ${CONFIG_DST}.tmp"
    exit 1
fi

ssh "$ROBOT" "mv ${CONFIG_DST}.tmp $CONFIG_DST"
echo "    done (${SIZE} bytes)"

echo "==> Restarting Valetudo ..."
ssh "$ROBOT" "kill \$(pidof valetudo) 2>/dev/null; sleep 2; VALETUDO_CONFIG_PATH=$CONFIG_DST /data/valetudo > /tmp/valetudo.log 2>&1 &"
sleep 3

STATUS=$(ssh "$ROBOT" "pidof valetudo && echo running || echo not running")
echo "    Valetudo: $STATUS"

echo ""
echo "Done. UI: http://$(ssh "$ROBOT" 'ip -4 addr show wlan0 2>/dev/null | grep -o "inet [0-9.]*" | cut -d" " -f2 || echo 192.168.1.213')"
