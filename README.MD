pPlay
======

> **Important:** this repository is a fork of the long-unmaintained original **pPlay** project by **cpasjuste**, while intentionally preserving the original name.
> The project continues to evolve in this fork, while the original architecture and core remain the work of the original author.

|                            Filer view                             |                            Player view                             |
|:-----------------------------------------------------------------:|:------------------------------------------------------------------:|
| ![](https://github.com/freetushkan/pPlay/raw/master/screenshot.jpg) | ![](https://github.com/freetushkan/pPlay/raw/master/screenshot2.jpg) |

pPlay is a video player, built around **MPV/FFmpeg**.

Supported features:
- local media playback;
- network sources (`http://`, `https://`, `ftp://`);
- SMB/Samba (`smb://`);
- TorrServer (`ts://` and `tss://`);
- subtitles and standard playback navigation.

pPlay uses the [MPV](https://mpv.io/) interface to handle FFmpeg interoperability, providing a fast, stable and powerful playback experience.

Installation
----

- **PS4:** install the `.pkg` file.
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
2. **TorrServer integration**: virtual torrent browsing, directory-like file navigation, automatic conversion to stream URLs for playback, watched-state support.
3. **Samba/SMB support**: native `smb://` support, automatic `smb2://` conversion for MPV backend, configurable SMB read buffer.
4. **Flexible network retry/timeouts**: separate playback retry count and connection timeout settings.
5. **UX/player options**: configurable seek intervals, autoplay-after-finish behavior, swapped controls mode, UI accent color, logging.
6. **Configuration stabilization**: validation of configuration values (for example SMB buffer range and HEX color validation).

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
* displays torrent file structures as virtual directories;
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

# Platform-specific
UMS_DEVICE = "ums0:/"
CPU_BOOST = "Enabled"
```

## MPV configuration

As before, you can place an `mpv.conf` file inside the `mpv` directory and use native MPV options directly.

See:

* [MPV configuration files](https://mpv.io/manual/master/#configuration-files)
* [MPV options](https://mpv.io/manual/master/#options)
