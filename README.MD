pPlay
======

> **Important:** this repository is a fork of the long-unmaintained original **pPlay** project by **cpasjuste**, while intentionally preserving the original name.
> The project continues to evolve in this fork, while the original architecture and core remain the work of the original author.

|                            Filer view                             |                            Player view                             |
|:-----------------------------------------------------------------:|:------------------------------------------------------------------:|
| ![](https://github.com/freetushkan/pPlay/raw/master/screenshot.jpg) | ![](https://github.com/freetushkan/pPlay/raw/master/screenshot2.jpg) |

pPlay is a video player, built around **MPV/FFmpeg**, that supports most popular video format, subtitles, network streaming and more

Supported features:
- local media playback;
- network sources (`http://`, `https://`, `ftp://`);
- SMB/Samba (`smb://`);
- TorrServer (`ts://` and `tss://`);
- subtitles and standard playback navigation.

pPlay uses the [MPV](https://mpv.io/) interface to handle FFmpeg interoperability, providing a fast, stable and powerful playback experience.

Installation
----

- **PS4:** just install the `.pkg` file from latest release.
- **Switch:** currently there is no build.

Usage
-----

- Use `LEFT/RIGHT` to switch interface sections and windows.
- During playback:
  - `LT/RT` / `LB/RB` — rewind/speed (can be swaped in settings);
  - `UP/DOWN` — volume control;
  - `CROSS` — pause playback;
  - `SQUARE` — open video/audio/subtitle track selection;
  - `CIRCLE` — minimize player;
  - `TRIANGLE` + `UP/DOWN` — brightness control;

Fork additions
----

Below are the main practical improvements and modernizations included in this fork:

1. **Multi-network profiles**: up to 5 independent network sources (`NETWORK1..NETWORK5`) with custom labels (`NETWORKx_NAME`) and remembered last directories (`NETWORKx_LAST`).
2. **TorrServer integration**: torrent browsing, files playback and watched-state support.
3. **Samba/SMB support**: custom `smb://` mpv handler, configurable SMB read buffer.
4. **Flexible network retry/timeouts**: separate playback retry count and connection timeout settings.
5. **UX/player options**: settings menu with configurable seek intervals, autoplay-after-finish behavior, swapped controls mode, UI accent color, logging and more.

TorrServer support
----

Supported schemes:
- `ts://` → internally treated as `http://`
- `tss://` → internally treated as `https://`

Example source:

```ini
NETWORK1 = "ts://192.168.1.10:8090/"
NETWORK1_NAME = "TorrServer LAN"
````

Secure source example:

```ini
NETWORK2 = "tss://my-torr.example.com:8091/"
NETWORK2_NAME = "TorrServer HTTPS"
```

Important notes:

* pPlay retrieves torrent lists using the TorrServer API;
* displays torrent file structures as directories;
* streams selected files directly;
* uses/resets watched marks on the TorrServer side.

## Samba (SMB) support

Connection example:

```ini
NETWORK3 = "smb://user:password@192.168.1.50/media/"
NETWORK3_NAME = "NAS Samba"
```

Recommendations:

* Start with `SMB_READ_BUFFER_MB = 10` (default).
* For unstable networks, increase the buffer size (`1..100` valid range).
* Increase `NETWORK_TIMEOUT` and `NETWORK_RETRIES` if playback interruptions occur.

## Known issues

* Long playback start time when resuming from a saved position (SMB).
* Playlist position resets to the first item when resuming after a long pause (M3U with HTTPS links).

## Current `pplay.cfg` parameters

Below is the list of currently supported and maintained configuration parameters.

### Network sources

* `NETWORK1..NETWORK5` — up to 5 network URLs (http/https/ftp/smb/ts/tss).
* `NETWORK1_NAME..NETWORK5_NAME` — source labels shown in UI.
* `NETWORK1_LAST..NETWORK5_LAST` — last opened directory for each source.

### Local paths and state

* `HOME_PATH` — default home directory.
* `LAST_LOCAL_PATH` — last opened local directory.
* `LAST_MODULE` — last active module (`LOCAL`, `NETWORK1..NETWORK5`, `USB`, etc.).

### Network and playback

* `NETWORK_TIMEOUT` (int, seconds, default `15`) — network operation timeout.
* `NETWORK_RETRIES` (int, default `3`) — playback retry attempts.
* `SMB_READ_BUFFER_MB` (int, `1..100`, default `10`) — SMB pre-buffer size.
* `AUTOPLAY_MODE` (int, default `1`) — behavior after playback ends.

### Controls and UI

* `SEEK_SHORT_SEC` (float, default `30`) — short seek interval.
* `SEEK_LONG_SEC` (float, default `300`) — long seek interval.
* `SWAP_CONTROLS` (`0/1`, default `0`) — swap seek/control button mapping.
* `ACCENT_COLOR` (HEX string, default `#1078C8`) — UI accent color.
* `LOG_LEVEL` (`0..4`, default `0`) — log verbosity: OFF/ERROR/INFO/DEBUG/TRACE.
* `CAST_ENABLED` (`0/1`, default `0`) — enables the experimental LAN Chromecast/DIAL receiver.
* `CAST_RECEIVER_NAME` (string, default `pPlay`) — friendly name announced to Android cast clients.
* `CAST_PORT` (int, default `8008`) — HTTP/DIAL control port; the Cast compatibility listener uses the next port when changed.

### Platform-specific

* `UMS_DEVICE` (Switch) — USB device mount path, default `ums0:/`.
* `CPU_BOOST` (Switch) — `Disabled` / `Enabled` for heavy media playback.
* `UTC_OFFSET` (PS4) — timezone offset.

## Example `pplay.cfg`

```ini
# Network slots
NETWORK1 = "http://samples.ffmpeg.org/"
NETWORK1_NAME = "HTTP Samples"

NETWORK2 = "ftp://user:password@192.168.1.20:21/"
NETWORK2_NAME = "FTP Server"

NETWORK3 = "smb://DOMAIN.user:password@192.168.1.50/media/"
NETWORK3_NAME = "NAS Samba"

NETWORK4 = "ts://192.168.1.10:8090/"
NETWORK4_NAME = "TorrServer"

NETWORK5 = "tss://user:password@192.168.1.10:8091/"
NETWORK5_NAME = "TorrServer HTTPS Auth"

# Paths/state
HOME_PATH = "/data/pplay/"
LAST_LOCAL_PATH = "/data/pplay/"
LAST_MODULE = "LOCAL"

# Network/player
NETWORK_TIMEOUT = 20
NETWORK_RETRIES = 5
SMB_READ_BUFFER_MB = 16
AUTOPLAY_MODE = 1

# Controls/UI
SEEK_SHORT_SEC = 15
SEEK_LONG_SEC = 120
SWAP_CONTROLS = 0
ACCENT_COLOR = "#1078C8"
LOG_LEVEL = 2
CAST_ENABLED = 0
CAST_RECEIVER_NAME = "pPlay"
CAST_PORT = 8008

# Platform-specific
UMS_DEVICE = "ums0:/"
CPU_BOOST = "Enabled"
```


## Chromecast / Android cast support

This fork includes an experimental local-network Chromecast compatibility service. When enabled, pPlay announces itself over DIAL/SSDP, exposes a Chromecast-like device description and provides HTTP control endpoints that can be used by Android notification controls or companion apps on the same LAN. The service writes its lifecycle, discovery and command handling messages to `pplay.log` using the regular `LOG_LEVEL` setting.

Useful debug endpoints while `CAST_ENABLED = 1`:

* `http://<pplay-ip>:8008/ssdp/device-desc.xml` — receiver description.
* `http://<pplay-ip>:8008/pplay/status` — current playback state.
* `http://<pplay-ip>:8008/pplay/control?command=playpause` — toggle playback.
* `http://<pplay-ip>:8008/pplay/control?command=seek&seconds=30` — seek relatively.
* `http://<pplay-ip>:8008/pplay/control?command=volume&delta=5` — adjust volume.
* `http://<pplay-ip>:8008/pplay/load?url=<media-url>` — load a network media URL and start playback.

The low-level Cast v2 TCP port is opened for compatibility logging, but full TLS/protobuf Cast session negotiation is still intentionally marked experimental.

## MPV configuration

As pPlay use [MPV](https://mpv.io/), you can place an `mpv.conf` file inside the `/data/pplay/mpv` directory and use native MPV options directly.

See:

* [MPV configuration files](https://mpv.io/manual/master/#configuration-files)
* [MPV options](https://mpv.io/manual/master/#options)
