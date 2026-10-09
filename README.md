# Linux Miracast Receiver

A Linux-based **Miracast / Wi-Fi Display (WFD) receiver** that mirrors an Android device's screen over a Wi-Fi Direct (P2P) connection. The project implements the receiver side of the connection in C++ and uses **GStreamer directly for media playback**—there is no `ffplay` process or MPEG-TS FIFO playback path in the current implementation.

The project brings together Linux Wi-Fi networking, WFD/RTSP negotiation, RTP/RTCP reception, and an in-process GStreamer pipeline.

## Features

- Creates a Wi-Fi Direct P2P group using `wpa_supplicant` and `wpa_cli`.
- Configures the P2P interface and provides DHCP through `dnsmasq`.
- Implements the Miracast/WFD RTSP sink in C++.
- Negotiates the WFD media session with the Android source.
- Receives RTP media over UDP and listens for RTCP on a separate UDP port.
- Removes the RTP header and passes MPEG-TS payload data into GStreamer's `appsrc`.
- Uses GStreamer to demultiplex MPEG-TS and decode/render H.264 video and AAC audio.
- Provides a small Python controller to send `PAUSE`, `PLAY` (resume), and `QUIT` commands to the running C++ sink.
- Includes a startup script that automates the network setup and launches the receiver as the desktop user.

## Architecture

```text
                         ANDROID PHONE
                          WFD Source
                               |
                         Wi-Fi Direct
                               |
                               v
                     +-------------------+
                     |   wpa_supplicant  |
                     |      wpa_cli      |
                     +---------+---------+
                               |
                         P2P group iface
                               |
                    192.168.49.0/24 network
                               |
                  +------------+------------+
                  |                         |
               dnsmasq                 C++ WFD Sink
              DHCP server               `startcast`
                                            |
                                      RTSP / WFD
                                       negotiation
                                            |
                                      RTP over UDP
                                       port 19000
                                            |
                                   RTP header parsing
                                            |
                                      MPEG-TS data
                                            |
                                      GStreamer
                                            |
                                         appsrc
                                            |
                                         tsdemux
                                      /           \
                                H.264 video     AAC audio
                                    |                |
                              parse/decode     parse/decode
                                    |                |
                             autovideosink     autoaudiosink
```

**Protocol layers:** Wi-Fi Direct provides connectivity; IP enables communication; RTSP/WFD negotiates the media session; RTP transports media; GStreamer handles demultiplexing, decoding, and playback.

## Repository layout

```text
MiracastR/
├── run2.sh                    # End-to-end startup and cleanup script
├── controller.py              # Local playback-control client
├── Makefile                   # Builds the C++ receiver
├── conf/
│   └── p2p_config_display     # wpa_supplicant configuration
└── src/
    ├── main.cpp               # RTSP/WFD session and control socket
    ├── config.h               # IP addresses and port configuration
    ├── rtsp_parser.cpp/.h     # RTSP message parsing
    ├── rtsp_utils.cpp/.h      # RTSP helpers
    ├── rtp_receiver.cpp/.h    # RTP/UDP receive and media payload handling
    ├── rtcp_receiver.cpp/.h   # RTCP/UDP receiver
    └── gstreamer_pipeline.cpp/.h # In-process GStreamer playback pipeline
```

## Requirements

The commands below target Ubuntu/Debian-based Linux. Package names can vary slightly by distribution.

### Build tools and development headers

```bash
sudo apt update
sudo apt install build-essential pkg-config python3 \
    wpa_supplicant wireless-tools iproute2 iputils-ping \
    dnsmasq libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
```

### GStreamer runtime plugins

The receiver creates GStreamer elements for MPEG-TS demultiplexing, H.264/AAC parsing and decoding, and audio/video output. Install the common plugin sets:

```bash
sudo apt install gstreamer1.0-tools gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly gstreamer1.0-libav
```

Your system also needs a working desktop audio/video session for `autovideosink` and `autoaudiosink` to select suitable output sinks.

### Wi-Fi adapter

The Wi-Fi adapter and driver must support Wi-Fi Direct/P2P through Linux `nl80211`. Availability and stability depend on the adapter, driver, and regulatory settings.

## Build

From the repository root:

```bash
make
```

This builds the `startcast` executable. To remove generated object files and the executable:

```bash
make clean
```

The Makefile uses `pkg-config` to obtain compiler and linker flags for `gstreamer-1.0` and `gstreamer-app-1.0`.

## Configuration

The current configuration contains machine-specific values that may need to be changed before running the project.

### Wi-Fi interface

Edit the interface near the top of `run2.sh`:

```bash
IFACE="wlp0s20f3"
```

Find your wireless interface with:

```bash
ip link
```

### Network addresses and ports

The current implementation expects the following values:

| Setting | Current value | Purpose |
|---|---:|---|
| Linux P2P address | `192.168.49.1` | Address assigned to the Linux P2P group interface |
| Android source address | `192.168.49.91` | Address used by the C++ sink to connect to the source's RTSP server |
| RTSP port | `7236` | RTSP/WFD session |
| RTP UDP port | `19000` | Incoming media packets |
| RTCP UDP port | `19001` | RTCP reception |
| Local controller | `127.0.0.1:9999` | Python-to-C++ control connection |
| P2P group frequency | `2437 MHz` | 2.4 GHz channel 6 |

The Android IP is currently hard-coded in `src/config.h`; the Linux IP and interface are also configured in the source/startup script. If your phone receives a different address, update the configuration consistently.

## Run

Build the project first, then start the full setup script from a terminal in the repository directory:

```bash
make
sudo ./run2.sh
```

The script automates the Wi-Fi Direct setup, starts the DHCP server, waits for the phone to connect, and launches `startcast` as the desktop user so GStreamer can access the graphical and audio sessions. The script may temporarily stop `NetworkManager` and the system `wpa_supplicant` while it configures the adapter. Avoid running it on a Wi-Fi interface you need for another active connection.

When the phone is connected and the sink is running, open the Android device's screen-casting / wireless-display UI and connect to the Linux receiver if it is not already connected.

Press **Ctrl+C** in the startup terminal to stop the session and run the script's cleanup routine.

> The startup script is tailored to the configuration of the machine on which it was developed. Review `run2.sh` before running it on another system, especially the interface name, IP settings, and service-management commands.

## Playback controls

`controller.py` connects to the C++ sink over loopback TCP at `127.0.0.1:9999`. It does not play media itself; it asks the sink to send RTSP control requests over the existing WFD session.

Run it in another terminal while `startcast` is running:

```bash
python3 controller.py
```

Commands:

- `p` — send RTSP `PAUSE`.
- `r` — send RTSP `PLAY` to resume.
- `q` — send `QUIT` to the C++ controller endpoint and exit the Python controller.

The phone may also send RTSP playback requests as part of its own screen-casting session.

## Media pipeline

The C++ RTP receiver listens for UDP media on port `19000`. It parses RTP packet headers and forwards the MPEG-TS payload to the GStreamer pipeline. The pipeline is created inside the receiver process; media does not pass through an external player or a named FIFO.

Conceptually, the pipeline is:

```text
RTP/UDP
   |
C++ RTP receiver
   |  strips RTP header
   v
MPEG-TS payload
   |
GStreamer appsrc
   |
 tsdemux
   +--------------------------+
   |                          |
 H.264 video                 AAC audio
   |                          |
 h264parse                   aacparse
   |                          |
 video decoder               AAC decoder
   |                          |
 autovideosink               audio conversion/resampling
                              |
                         autoaudiosink
```

GStreamer starts when valid media data arrives. The exact output device and sink selected by `autovideosink` / `autoaudiosink` depend on the Linux desktop environment and installed plugins.

## RTSP / WFD session

The C++ receiver handles the RTSP control channel and negotiates the WFD session. The exchange can include methods such as:

- `OPTIONS`
- `GET_PARAMETER`
- `SET_PARAMETER`
- `SETUP`
- `PLAY`
- `PAUSE`
- `TEARDOWN`

A simplified flow is:

```text
Android WFD Source                     Linux WFD Sink
       |                                      |
       |----------- RTSP session ------------>|
       |<---------- WFD negotiation ----------|
       |<------------- SETUP -----------------|
       |<-------------- PLAY -----------------|
       |                                      |
       |========== RTP media over UDP =======>|
       |                                      |----> GStreamer
       |<------------- RTCP ----------------->|      playback
```

The precise message order and which side initiates particular requests depend on the source implementation and session state.

## Troubleshooting

### Check the wireless interface

```bash
ip link
```

Make sure the interface configured in `run2.sh` exists and supports P2P operations.

### Check the DHCP lease and logs

```bash
cat /tmp/miracast-dnsmasq.leases
cat /tmp/miracast-dnsmasq.log
```

Confirm that the phone received the address expected by `src/config.h` (`192.168.49.91` by default).

### Check connectivity

```bash
ping -c 3 192.168.49.91
```

A failed ping is not conclusive by itself, since a device or firewall may not respond to ICMP. Check the DHCP lease and RTSP connection logs as well.

### Check GStreamer plugins

Inspect the installed GStreamer environment:

```bash
gst-inspect-1.0 tsdemux
gst-inspect-1.0 h264parse
gst-inspect-1.0 avdec_h264
gst-inspect-1.0 aacparse
gst-inspect-1.0 avdec_aac
```

If the receiver reports that an element cannot be created, install the GStreamer plugin package that provides that element. Video/audio output issues can also be caused by the desktop session or output device.

### Port already in use

Check whether another process is listening on the configured ports:

```bash
sudo ss -lntup | grep -E ':(7236|19000|19001|9999)\b'
```

Stop any stale receiver process before starting another instance.

## Project learnings

This project explores the interaction of several system layers:

- **Linux networking:** interfaces, Wi-Fi Direct, P2P groups, IP addressing, DHCP, and UDP/TCP sockets.
- **Miracast / Wi-Fi Display:** source/sink roles and WFD capability negotiation.
- **RTSP:** request/response handling, session state, sequence numbers, and playback control.
- **RTP/RTCP:** UDP media reception, RTP header parsing, payload handling, and a separate RTCP socket.
- **Multimedia:** MPEG-TS demultiplexing, H.264/AAC decoding, timestamps, and audio/video output through GStreamer.
- **Linux integration:** shell automation, process cleanup, desktop-session environment, and debugging with network tools.

## Possible future improvements

- Discover the source IP dynamically instead of hard-coding it.
- Make interface and port settings configurable through command-line options or a config file.
- Improve RTSP error handling and reconnect behavior.
- Harden shutdown and resource cleanup paths.
- Improve media timing, buffering, and audio/video synchronization.
- Add clearer runtime diagnostics for missing GStreamer elements and output-device failures.

## License

See [`LICENSE`](LICENSE).
