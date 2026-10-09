#!/usr/bin/env bash

set -u

# ==================================================
# CONFIGURATION
# ==================================================

IFACE="wlp0s20f3"

WPA_CONF="/etc/wpa_supplicant/p2p_config_display"

LINUX_IP="192.168.49.1"

GROUP_FREQ="2437"

LEASE_FILE="/tmp/miracast-dnsmasq.leases"
DNSMASQ_LOG="/tmp/miracast-dnsmasq.log"


# ==================================================
# PROJECT PATH
# ==================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

WPA_SRC="$SCRIPT_DIR/conf/p2p_config_display"


# ==================================================
# PROCESS VARIABLES
# ==================================================

GROUP_IFACE=""

DNSMASQ_PID=""

SINK_PID=""

DEVICE_IP=""


# ==================================================
# FUNCTIONS
# ==================================================

run_wpa()
{
    wpa_cli -i "$IFACE" "$@"
}


die()
{
    echo
    echo "[-] $1"
    cleanup
    exit 1
}


cleanup()
{
    echo
    echo "=========================================="
    echo "                 CLEANUP"
    echo "=========================================="


    # ----------------------------------------------
    # Stop Miracast sink
    # ----------------------------------------------

    if [ -n "${SINK_PID:-}" ]; then

        echo "[+] Stopping Miracast sink..."

        kill "$SINK_PID" 2>/dev/null || true

        wait "$SINK_PID" 2>/dev/null || true

        SINK_PID=""

    fi


    # ----------------------------------------------
    # Kill any leftover startcast
    # ----------------------------------------------

    pkill -x startcast 2>/dev/null || true


    # ----------------------------------------------
    # Stop dnsmasq
    # ----------------------------------------------

    if [ -n "${DNSMASQ_PID:-}" ]; then

        echo "[+] Stopping dnsmasq..."

        kill "$DNSMASQ_PID" 2>/dev/null || true

        wait "$DNSMASQ_PID" 2>/dev/null || true

        DNSMASQ_PID=""

    fi


    # ----------------------------------------------
    # Remove DHCP files
    # ----------------------------------------------

    rm -f "$LEASE_FILE"


    # ----------------------------------------------
    # Restore networking
    # ----------------------------------------------

    echo "[+] Stopping P2P wpa_supplicant..."

    pkill -x wpa_supplicant 2>/dev/null || true


    echo "[+] Starting NetworkManager..."

    systemctl start NetworkManager 2>/dev/null || true


    echo "[+] Starting system wpa_supplicant..."

    systemctl start wpa_supplicant 2>/dev/null || true


    echo
    echo "[+] Cleanup complete"
}


# ==================================================
# SIGNAL HANDLING
# ==================================================

trap cleanup INT TERM


# ==================================================
# ROOT CHECK
# ==================================================

if [ "$EUID" -ne 0 ]; then

    echo "[-] Please run with sudo"

    exit 1

fi


# ==================================================
# DETERMINE DESKTOP USER
# ==================================================

USER_NAME="${SUDO_USER:-}"


if [ -z "$USER_NAME" ] || [ "$USER_NAME" = "root" ]; then

    USER_NAME="$(logname 2>/dev/null || true)"

fi


if [ -z "$USER_NAME" ] || [ "$USER_NAME" = "root" ]; then

    echo "[-] Could not determine the normal desktop user."

    echo
    echo "    Run this script using:"
    echo
    echo "    sudo ./run2.sh"
    echo

    exit 1

fi


USER_ID="$(id -u "$USER_NAME")"

USER_HOME="$(getent passwd "$USER_NAME" | cut -d: -f6)"


if [ -z "$USER_ID" ] || [ -z "$USER_HOME" ]; then

    die "Could not determine desktop user information"

fi


# ==================================================
# START
# ==================================================

echo
echo "=========================================="
echo "       Linux Miracast Receiver"
echo "          GStreamer Edition"
echo "=========================================="


echo
echo "[+] Project directory : $SCRIPT_DIR"
echo "[+] Desktop user      : $USER_NAME"
echo "[+] Desktop UID       : $USER_ID"
echo "[+] Desktop home      : $USER_HOME"
echo "[+] Wi-Fi interface   : $IFACE"


# ==================================================
# 1. INSTALL WPA CONFIG
# ==================================================

echo
echo "[1] Installing wpa_supplicant configuration..."


if [ ! -f "$WPA_SRC" ]; then

    die "WPA configuration not found: $WPA_SRC"

fi


cp "$WPA_SRC" "$WPA_CONF"


if [ $? -ne 0 ]; then

    die "Failed to copy WPA configuration"

fi


chmod 600 "$WPA_CONF"


echo "[+] Configuration copied:"
echo "    $WPA_SRC"
echo "       ->"
echo "    $WPA_CONF"


# ==================================================
# 2. CLEAN PREVIOUS MIRACAST PROCESSES
# ==================================================

echo
echo "[2] Cleaning previous Miracast processes..."


if pgrep -x startcast >/dev/null 2>&1; then

    echo "[+] Stopping existing startcast..."

    pkill -TERM -x startcast 2>/dev/null || true

    sleep 1

    pkill -KILL -x startcast 2>/dev/null || true

else

    echo "[+] No previous startcast process found"

fi


pkill -f "dnsmasq.*p2p-" 2>/dev/null || true


# ==================================================
# 3. STOP NETWORKMANAGER
# ==================================================

echo
echo "[3] Stopping NetworkManager..."


systemctl stop NetworkManager 2>/dev/null || true


echo "[+] Stopping existing wpa_supplicant..."


systemctl stop wpa_supplicant 2>/dev/null || true

pkill -x wpa_supplicant 2>/dev/null || true


sleep 1


# ==================================================
# 4. START WPA_SUPPLICANT
# ==================================================

echo
echo "[4] Starting wpa_supplicant..."


wpa_supplicant \
    -i "$IFACE" \
    -D nl80211 \
    -c "$WPA_CONF" \
    -B


sleep 2


run_wpa status >/dev/null || die "wpa_supplicant did not start"


echo "[+] wpa_supplicant started"


# ==================================================
# 5. CONFIGURE WFD
# ==================================================

echo
echo "[5] Configuring WFD..."


run_wpa wfd_subelem_set 0 000600111c4400c8


echo "[+] WFD configured"


# ==================================================
# 6. P2P DISCOVERY
# ==================================================

echo
echo "[6] Starting P2P discovery..."


run_wpa p2p_find


sleep 5


echo "[+] P2P discovery period complete"


# ==================================================
# 7. CREATE P2P GROUP
# ==================================================

echo
echo "[7] Creating P2P group..."


run_wpa p2p_group_add freq="$GROUP_FREQ"


echo "[+] Waiting for P2P group interface..."


GROUP_IFACE=""


for ((i=0; i<20; i++)); do

    GROUP_IFACE=$(ip -o link show | \
        awk -F': ' '$2 ~ /^p2p-/ {print $2}' | \
        cut -d'@' -f1 | \
        head -n1)


    if [ -n "$GROUP_IFACE" ]; then
        break
    fi


    sleep 1

done


if [ -z "$GROUP_IFACE" ]; then

    die "P2P group interface was not created"

fi


echo "[+] P2P interface: $GROUP_IFACE"


# ==================================================
# 8. WPS PBC
# ==================================================

echo
echo "[8] Starting WPS PBC..."


echo "[WPS] PBC on $IFACE..."

run_wpa wps_pbc


echo "[WPS] PBC on $GROUP_IFACE..."

wpa_cli -i "$GROUP_IFACE" wps_pbc || true


# ==================================================
# 9. CONFIGURE P2P INTERFACE
# ==================================================

echo
echo "[9] Preparing P2P interface..."


ip addr add "$LINUX_IP/24" dev "$GROUP_IFACE" 2>/dev/null || true

ip link set dev "$GROUP_IFACE" up


echo "[+] $GROUP_IFACE -> $LINUX_IP"


# ==================================================
# 10. START DHCP SERVER
# ==================================================

echo
echo "[10] Starting DHCP server..."


rm -f "$LEASE_FILE"

rm -f "$DNSMASQ_LOG"


dnsmasq \
    --port=0 \
    -i "$GROUP_IFACE" \
    --dhcp-range=192.168.49.2,192.168.49.100,24h \
    --dhcp-leasefile="$LEASE_FILE" \
    --log-queries \
    --log-dhcp \
    >"$DNSMASQ_LOG" 2>&1 &


DNSMASQ_PID=$!


echo "[+] dnsmasq started"

echo "    PID: $DNSMASQ_PID"


# ==================================================
# 11. WAIT FOR ANY DHCP CLIENT
# ==================================================

echo
echo "[WAIT] Waiting for Miracast device DHCP lease..."


DEVICE_IP=""


for ((i=0; i<60; i++)); do

    if [ -f "$LEASE_FILE" ]; then

        DEVICE_IP=$(awk 'NF >= 3 {print $3; exit}' "$LEASE_FILE")


        if [ -n "$DEVICE_IP" ]; then
            break
        fi

    fi


    sleep 1

done


if [ -z "$DEVICE_IP" ]; then

    echo "[-] No DHCP client connected."

    echo
    echo "[+] dnsmasq log:"

    cat "$DNSMASQ_LOG"


    cleanup

    exit 1

fi


echo "[+] Miracast device connected!"


echo
echo "------------------------------------------"
echo " DHCP lease obtained"
echo " Device IP : $DEVICE_IP"
echo " P2P       : $GROUP_IFACE"
echo "------------------------------------------"


# ==================================================
# 12. CHECK DEVICE CONNECTIVITY
# ==================================================

echo
echo "[12] Checking device connectivity..."


DEVICE_REACHABLE=0


for ((i=0; i<10; i++)); do

    if ping -c 1 -W 1 "$DEVICE_IP" >/dev/null 2>&1; then

        DEVICE_REACHABLE=1

        echo "[+] Device reachable at $DEVICE_IP"

        break

    fi


    sleep 1

done


if [ "$DEVICE_REACHABLE" -ne 1 ]; then

    echo "[-] Device is not responding to ping"

fi


# ==================================================
# 13. START MIRACAST SINK
# ==================================================

echo
echo "[13] Starting Miracast sink..."


cd "$SCRIPT_DIR"


echo "[+] Starting startcast as desktop user: $USER_NAME"


sudo -u "$USER_NAME" \
    env \
    HOME="$USER_HOME" \
    USER="$USER_NAME" \
    LOGNAME="$USER_NAME" \
    DISPLAY="${DISPLAY:-:0}" \
    XDG_RUNTIME_DIR="/run/user/$USER_ID" \
    WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}" \
    PULSE_RUNTIME_PATH="/run/user/$USER_ID/pulse" \
    ./startcast &

SINK_PID=$!


echo "[+] Miracast sink PID: $SINK_PID"


echo
echo "[WAIT] Waiting for Miracast sink to establish..."


sleep 5


# ==================================================
# 14. CHECK SINK
# ==================================================

if ! kill -0 "$SINK_PID" 2>/dev/null; then

    echo
    echo "[-] Miracast sink exited unexpectedly."

    cleanup

    exit 1

fi


echo "[+] Miracast sink is running."


# ==================================================
# 15. RUNNING
# ==================================================

echo
echo "=========================================="
echo "       MIRACAST + GSTREAMER IS RUNNING"
echo "=========================================="


echo
echo "[+] P2P interface : $GROUP_IFACE"
echo "[+] Linux IP      : $LINUX_IP"
echo "[+] Device IP     : $DEVICE_IP"
echo "[+] RTSP port     : 7236"
echo "[+] Media backend : GStreamer appsrc"


echo
echo "[+] GStreamer is handled internally"
echo "    by startcast."


echo
echo "[+] Press Ctrl+C to stop."


# ==================================================
# 16. WAIT FOR MIRACAST SINK
# ==================================================

wait "$SINK_PID"


# ==================================================
# 17. CLEANUP
# ==================================================

cleanup