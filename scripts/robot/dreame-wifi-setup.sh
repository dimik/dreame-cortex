#!/bin/bash
LOG=/home/dp/dreame-wifi-setup.log
exec > "$LOG" 2>&1

HOME_SSID="5K"
HOME_PASS='LHzeYe87tbo@dRR.vYzEkFs8R'
ROBOT_AP="dreame-vacuum-r2250_miap8E6A"
SSH_KEY="/home/dp/.ssh/id_rsa_dreame"

reconnect_home() {
    echo "==> Returning to home WiFi..."
    nmcli con up "$HOME_SSID" 2>&1
    echo "==> Done. Connected: $(iwconfig wlo2 2>/dev/null | grep ESSID)"
}
trap reconnect_home EXIT

echo "==> Disconnecting from home WiFi..."
nmcli dev disconnect wlo2 2>&1
sleep 3

echo "==> Connecting to robot AP..."
nmcli dev wifi connect "$ROBOT_AP" ifname wlo2 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: Could not connect to robot AP"
    exit 1
fi
sleep 4
echo "==> IP: $(ip addr show wlo2 | grep 'inet ' | awk '{print $2}')"

echo "==> SSHing into robot and checking WiFi tools..."
ssh -i "$SSH_KEY" -o StrictHostKeyChecking=no -o ConnectTimeout=10 root@192.168.5.1 "
    echo '--- hostname ---'
    hostname
    echo '--- wifi tools ---'
    which nmcli wpa_cli iwconfig uci 2>/dev/null
    echo '--- network config files ---'
    ls /etc/wpa_supplicant* /etc/network* /etc/config/wireless 2>/dev/null
    echo '--- valetudo wifi capability ---'
    ls /etc/valetudo/ 2>/dev/null
    cat /etc/valetudo/valetudo.json 2>/dev/null | head -20
"

echo ""
echo "==> Configuring WiFi on robot via SSH using wpa_cli..."
ssh -i "$SSH_KEY" -o StrictHostKeyChecking=no -o ConnectTimeout=10 root@192.168.5.1 bash << ENDSSH
set -x
IFACE=\$(wpa_cli interface 2>/dev/null | tail -1)
echo "wpa_cli interface: \$IFACE"
NET_ID=\$(wpa_cli -i \$IFACE add_network | tail -1)
echo "Network ID: \$NET_ID"
wpa_cli -i \$IFACE set_network \$NET_ID ssid '"$HOME_SSID"'
wpa_cli -i \$IFACE set_network \$NET_ID psk '"$HOME_PASS"'
wpa_cli -i \$IFACE set_network \$NET_ID key_mgmt WPA-PSK
wpa_cli -i \$IFACE enable_network \$NET_ID
wpa_cli -i \$IFACE save_config
wpa_cli -i \$IFACE reconfigure
echo "WiFi config done"
ENDSSH

echo "==> WiFi configured on robot, rebooting..."
ssh -i "$SSH_KEY" -o StrictHostKeyChecking=no -o ConnectTimeout=10 root@192.168.5.1 "reboot" 2>&1 || true

echo "==> Reconnecting laptop to home WiFi and waiting for robot..."
reconnect_home
trap - EXIT

sleep 20
echo "==> Scanning for robot on home network..."
for i in $(seq 1 24); do
    ROBOT_IP=$(arp -a 2>/dev/null | grep -v "192.168.1.1\b" | grep "192.168.1\." | awk '{print $2}' | tr -d '()' | head -1)
    if [ -n "$ROBOT_IP" ]; then
        echo "==> Robot found at $ROBOT_IP!"
        ssh -i "$SSH_KEY" -o StrictHostKeyChecking=no -o ConnectTimeout=10 root@$ROBOT_IP "hostname && echo SSH_OK" 2>&1
        # Update SSH config with real IP
        sed -i "s/HostName 192.168.1.100/HostName $ROBOT_IP/" /home/dp/.ssh/config
        echo "==> SSH config updated with robot IP: $ROBOT_IP"
        break
    fi
    echo "Attempt $i/24, waiting..."
    sleep 5
done
echo "==> DONE"
