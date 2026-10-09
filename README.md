# Linux Miracast Receiver

A Linux-based **Miracast / Wi-Fi Display (WFD) receiver** that allows an Android device to mirror its display to a Linux system over a Wi-Fi Direct P2P connection.

The project was built from the ground up in two major stages:

1. **Establish the Wi-Fi Direct network**
2. **Implement the Miracast/WFD RTSP session and receive the media stream**

The received MPEG-TS stream is played using `ffplay`, while a Python RTSP controller is used to experiment with playback control such as `PLAY`, `PAUSE`, and `RESUME`.

---

## Architecture

```text
                         ANDROID PHONE
                         Miracast Source
                               │
                               │
                    Wi-Fi Direct / P2P
                               │
                               ▼
                    ┌──────────────────┐
                    │  wpa_supplicant  │
                    │     wpa_cli       │
                    └────────┬─────────┘
                             │
                       P2P Group
                             │
                             ▼
                    ┌──────────────────┐
                    │   p2p interface  │
                    │  192.168.49.1    │
                    └────────┬─────────┘
                             │
                           DHCP
                         dnsmasq
                             │
                             ▼
                    IP connectivity
                             │
                             │
                    ┌────────▼─────────┐
                    │   Miracast Sink  │
                    │     startcast    │
                    └────────┬─────────┘
                             │
                      RTSP / WFD
                       negotiation
                             │
                             ▼
                       RTP media
                             │
                             ▼
                        MPEG-TS
                             │
                             ▼
                     /tmp/miracast.ts
                         (FIFO)
                             │
                             ▼
                          ffplay
                             │
                             ▼
                      Android Screen
```

---

# How It Works

The receiver is divided into two main stages.

```text
        STAGE 1                         STAGE 2

   Wi-Fi Direct / P2P              Miracast / WFD
   ──────────────────              ──────────────

   wpa_supplicant                  RTSP
         │                           │
      wpa_cli                         │
         │                           │
    P2P group                         │
         │                           │
    P2P interface                     │
         │                           │
       DHCP                            │
         │                           │
         └──────── IP connectivity ──►│
                                     │
                              WFD negotiation
                                     │
                                  RTP media
                                     │
                                  MPEG-TS
                                     │
                                  ffplay
```

The important concept is that **Wi-Fi Direct and Miracast are separate layers**.

Wi-Fi Direct provides the network connection.

Miracast/WFD then uses that IP connection to establish the RTSP session and transport the media.

---

# 1. Wi-Fi Direct Setup

The Linux machine takes control of the Wi-Fi interface using `wpa_supplicant`.

The startup script first stops `NetworkManager` and any existing `wpa_supplicant` instance:

```bash
systemctl stop NetworkManager
systemctl stop wpa_supplicant
pkill -x wpa_supplicant
```

This avoids another network manager interfering with the P2P interface.

`wpa_supplicant` is then started using the Linux `nl80211` driver:

```bash
wpa_supplicant \
    -i "$IFACE" \
    -D nl80211 \
    -c "$WPA_CONF" \
    -B
```

---

# 2. Configure Wi-Fi Display

The receiver configures the WFD subelement through `wpa_cli`:

```bash
wpa_cli -i "$IFACE" \
    wfd_subelem_set 0 000600111c4400c8
```

This allows the P2P/WFD negotiation to advertise the required Wi-Fi Display information.

---

# 3. P2P Discovery

The script starts P2P discovery:

```bash
wpa_cli -i "$IFACE" p2p_find
```

After allowing discovery to run, it is stopped:

```bash
wpa_cli -i "$IFACE" p2p_find_stop
```

---

# 4. Create the P2P Group

The Linux system creates a P2P group at:

```text
Frequency: 2437 MHz
Channel:   6
```

using:

```bash
wpa_cli -i "$IFACE" p2p_group_add freq=2437
```

The resulting P2P group interface is then detected dynamically.

For example:

```text
p2p-wlp0s20f3-0
```

The exact interface name depends on the system.

---

# 5. WPS PBC

WPS Push Button Configuration is started on both interfaces:

```bash
wpa_cli -i "$IFACE" wps_pbc
wpa_cli -i "$GROUP_IFACE" wps_pbc
```

This allows the Android device to establish the Wi-Fi Direct group connection.

---

# 6. Configure the Linux P2P Interface

The Linux side uses:

```text
192.168.49.1/24
```

The interface is configured using:

```bash
ip addr add 192.168.49.1/24 dev "$GROUP_IFACE"
ip link set dev "$GROUP_IFACE" up
```

The resulting network is:

```text
192.168.49.0/24
```

with Linux acting as:

```text
Linux P2P interface
        │
        └── 192.168.49.1
```

---

# 7. DHCP Server

`dnsmasq` is used as the DHCP server for the P2P network.

The DHCP range is:

```text
192.168.49.2
        ↓
192.168.49.100
```

The relevant configuration is:

```bash
dnsmasq \
    --port=0 \
    -i "$GROUP_IFACE" \
    --dhcp-range=192.168.49.2,192.168.49.100,24h
```

The script waits until the Android device obtains a DHCP lease.

The expected Android device is:

```text
MAC: 4e:b8:8c:f8:24:35
IP : 192.168.49.91
```

The DHCP lease is verified through the `dnsmasq` lease file and log.

---

# Network Topology

Once the connection is established:

```text
┌─────────────────────┐
│       Android       │
│   Miracast Source   │
│                     │
│  192.168.49.91      │
└──────────┬──────────┘
           │
           │ Wi-Fi Direct
           │
           │ 192.168.49.0/24
           │
┌──────────▼──────────┐
│        Linux        │
│   Miracast Sink     │
│                     │
│  192.168.49.1       │
└─────────────────────┘
```

The script also verifies connectivity using:

```bash
ping -c 1 192.168.49.91
```

---

# 8. Start the Miracast Sink

After the P2P network is ready, the actual Miracast receiver is started:

```bash
./startcast &
```

The sink establishes the Miracast/WFD RTSP session with the Android source.

---

# Miracast / RTSP Layer

Once the network connection exists, the project moves from the networking layer into the actual Miracast protocol.

The Android device acts as the:

```text
WFD Source
```

while Linux acts as the:

```text
WFD Sink
```

The RTSP session is responsible for negotiating the media session.

The communication includes RTSP methods such as:

```text
OPTIONS
GET_PARAMETER
SET_PARAMETER
SETUP
PLAY
PAUSE
```

A simplified session looks like:

```text
Android                         Linux
 Source                          Sink
   │                               │
   │────── RTSP connection ───────►│
   │                               │
   │◄──────── OPTIONS ─────────────│
   │                               │
   │──── WFD negotiation ─────────►│
   │                               │
   │◄──── GET_PARAMETER ───────────│
   │                               │
   │──── SET_PARAMETER ───────────►│
   │                               │
   │◄──────── SETUP ───────────────│
   │                               │
   │─────── PLAY ─────────────────►│
   │                               │
   │══════ RTP media packets ═════►│
   │                               │
```

Correct RTSP session handling is required before the Android source will begin sending the media stream.

---

# Media Pipeline

After successful negotiation, the Android device sends the media stream to the Linux sink.

The pipeline is:

```text
Android Display
      │
      ▼
   H.264
      │
      ▼
     RTP
      │
      ▼
Miracast Sink
      │
      ▼
  MPEG-TS
      │
      ▼
/tmp/miracast.ts
      │
      ▼
    ffplay
      │
      ▼
Linux Display
```

The project uses a named pipe:

```text
/tmp/miracast.ts
```

created with:

```bash
mkfifo /tmp/miracast.ts
```

The Miracast sink writes the MPEG-TS stream into the FIFO while `ffplay` consumes it.

This provides a simple producer/consumer media pipeline:

```text
        Producer                    Consumer

     Miracast Sink                 ffplay
          │                           ▲
          │                           │
          └──── /tmp/miracast.ts ─────┘
                 named FIFO
```

---

# Low-Latency Playback

`ffplay` is started with low-latency options:

```bash
ffplay \
    -fflags nobuffer \
    -flags low_delay \
    -framedrop \
    -sync ext \
    -f mpegts \
    /tmp/miracast.ts
```

These options are intended to reduce buffering and keep playback close to real time.

Because the main startup script runs with root privileges, `ffplay` is explicitly launched as the normal desktop user:

```bash
sudo -u "$USER_NAME" ...
```

with the required desktop environment variables:

```text
HOME
USER
LOGNAME
XDG_RUNTIME_DIR
DISPLAY
XAUTHORITY
```

This allows `ffplay` to access the graphical desktop correctly.

---

# RTSP Control

A separate Python script is used to interact with the RTSP session.

The controller can send commands such as:

```text
[p] Pause
[r] Resume
[q] Quit
```

Conceptually:

```text
             Python Controller
                     │
                     │ RTSP
                     ▼
             Miracast RTSP Session
                     │
          ┌──────────┼──────────┐
          │          │          │
         PLAY       PAUSE      RESUME
          │          │          │
          └──────────┼──────────┘
                     │
                     ▼
              Media Session
```

This was also used to inspect and test how the Android Miracast source responds to RTSP control messages.

---

# Complete End-to-End Flow

The entire project can be summarized as:

```text
┌──────────────────────────────────────────────────────────┐
│                      ANDROID PHONE                       │
│                   Miracast / WFD Source                  │
└───────────────────────────┬──────────────────────────────┘
                            │
                            │ Wi-Fi Direct
                            ▼
┌──────────────────────────────────────────────────────────┐
│                    wpa_supplicant                        │
│                       wpa_cli                             │
│                                                          │
│   P2P discovery → group formation → WPS → P2P interface │
└───────────────────────────┬──────────────────────────────┘
                            │
                            ▼
                    192.168.49.0/24
                            │
                     ┌──────┴──────┐
                     │             │
                     ▼             ▼
              Linux .1       Android .91
                     │
                     │ DHCP
                     │
                     ▼
                  dnsmasq
                     │
                     ▼
┌──────────────────────────────────────────────────────────┐
│                     Miracast Sink                        │
│                        startcast                         │
│                                                          │
│                RTSP / WFD negotiation                    │
└───────────────────────────┬──────────────────────────────┘
                            │
                            │ RTP
                            ▼
                       MPEG-TS
                            │
                            ▼
                   /tmp/miracast.ts
                            │
                            ▼
                         ffplay
                            │
                            ▼
                     Linux Display
```

---

# Startup Script

The complete startup sequence is automated by the main shell script.

The script performs:

```text
1.  Check root privileges
2.  Determine desktop user
3.  Stop NetworkManager
4.  Stop existing wpa_supplicant
5.  Start wpa_supplicant
6.  Configure WFD
7.  Start P2P discovery
8.  Create P2P group
9.  Detect P2P interface
10. Start WPS PBC
11. Configure Linux P2P IP
12. Start dnsmasq
13. Wait for Android DHCP lease
14. Verify phone connectivity
15. Create MPEG-TS FIFO
16. Start Miracast sink
17. Start ffplay
18. Wait for sink
19. Stop ffplay when sink exits
20. Stop DHCP server
21. Restore NetworkManager
22. Restart system wpa_supplicant
```

This makes the entire receiver startup process reproducible with a single command.

---

# Requirements

## Hardware

* Linux PC/laptop
* Wi-Fi adapter supporting Wi-Fi Direct/P2P
* Android device supporting Miracast / Wi-Fi Display

## Software

* Linux
* `wpa_supplicant`
* `wpa_cli`
* `dnsmasq`
* Python 3
* FFmpeg / `ffplay`
* `iproute2`
* `ping`

Check the required tools:

```bash
wpa_supplicant -v
wpa_cli -v
dnsmasq --version
python3 --version
ffplay -version
ip -V
```

---

# Running

The startup script must be executed with root privileges:

```bash
sudo ./<startup-script>.sh
```

The script will then:

```text
Wi-Fi Direct setup
       ↓
P2P group creation
       ↓
DHCP
       ↓
Android connection
       ↓
Miracast sink
       ↓
RTSP negotiation
       ↓
RTP media
       ↓
MPEG-TS
       ↓
ffplay
```

Once the sink is running, the Android device can initiate a Miracast connection.

---

# Debugging

Useful commands while developing/testing the receiver:

### Inspect interfaces

```bash
ip link
ip addr
iw dev
```

### Check wpa_supplicant

```bash
wpa_cli -i wlp0s20f3 status
```

### Monitor P2P state

```bash
wpa_cli -i wlp0s20f3
```

### Check DHCP leases

```bash
cat /tmp/miracast-dnsmasq.leases
```

### Check dnsmasq logs

```bash
cat /tmp/miracast-dnsmasq.log
```

### Test Android connectivity

```bash
ping 192.168.49.91
```

### Inspect RTSP traffic

```bash
tcpdump -i <p2p-interface> port 7236
```

### Inspect UDP traffic

```bash
tcpdump -i <p2p-interface> udp
```

---

# RTSP Testing

The RTSP server can be tested independently.

For example:

```bash
printf 'OPTIONS * RTSP/1.0\r\nCSeq: 2\r\n\r\n' | \
nc -v 192.168.49.91 7236
```

A successful response contains an RTSP status such as:

```text
RTSP/1.0 200 OK
```

This is useful for debugging the RTSP layer without relying entirely on the Android Miracast UI.

---

# Project Components

```text
miracast-receiver/
│
├── startup.sh
│
├── startcast
│
├── controller.py
│
├── <RTSP / receiver source>
│
├── <wpa_supplicant configuration>
│
└── README.md
```

The main components are:

| Component           | Purpose                                 |
| ------------------- | --------------------------------------- |
| `startup.sh`        | Automates the complete receiver startup |
| `wpa_supplicant`    | Provides Wi-Fi Direct/P2P functionality |
| `wpa_cli`           | Controls P2P and WFD configuration      |
| `dnsmasq`           | Provides DHCP for the P2P network       |
| `startcast`         | Miracast/WFD sink                       |
| RTSP implementation | Handles WFD session negotiation         |
| `controller.py`     | Sends RTSP playback/control commands    |
| `/tmp/miracast.ts`  | MPEG-TS named pipe                      |
| `ffplay`            | Displays the received video             |

---

# What I Learned

This project involved working across several layers of a real multimedia networking stack:

### Linux networking

* Network interfaces
* Wi-Fi Direct
* P2P groups
* `wpa_supplicant`
* `wpa_cli`
* DHCP
* `dnsmasq`
* TCP/UDP

### Miracast / WFD

* Wi-Fi Display architecture
* WFD negotiation
* RTSP session management
* RTSP control methods
* Source/Sink roles

### Multimedia

* RTP
* H.264 streaming
* MPEG-TS
* Named FIFOs
* FFmpeg / FFplay
* Low-latency playback

### Debugging

* Network traffic inspection
* RTSP packet analysis
* DHCP lease debugging
* Linux process management
* Interface and connectivity debugging

---

# Lessons From the Architecture

One of the main takeaways from this project is that a Miracast connection is not a single protocol.

It is a stack:

```text
Miracast
   │
   ├── Wi-Fi Direct
   │
   ├── IP networking
   │
   ├── RTSP / WFD
   │
   ├── RTP
   │
   ├── H.264
   │
   └── Media playback
```

Each layer has a different responsibility.

```text
Wi-Fi Direct
    ↓
Creates the network

DHCP / IP
    ↓
Provides connectivity

RTSP / WFD
    ↓
Negotiates the Miracast session

RTP
    ↓
Transports media

MPEG-TS / FFplay
    ↓
Processes and displays the stream
```

Understanding and debugging the project therefore required following the data from the **wireless P2P layer all the way up to the video display**.

---

# Future Improvements

Possible extensions include:

* Automatic discovery of Android Miracast sources
* Removing hard-coded MAC/IP configuration
* Automatic IP detection
* More robust RTSP state management
* Automatic reconnect
* Audio support
* Hardware-accelerated H.264 decoding
* Native FFmpeg/GStreamer integration
* Better RTSP error handling
* Graceful shutdown using shell traps
* Support for multiple Miracast sources

---

## License

```text
MIT License
```

---

## Author

Built as a hands-on Linux multimedia and networking project exploring **Wi-Fi Direct, Miracast/WFD, RTSP, RTP, MPEG-TS and real-time video streaming**.

