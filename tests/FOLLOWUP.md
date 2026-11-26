# Follow-up regression tests (Linux)

Run `bash tests/run-followup.sh` after preparing the pinned embedded core with
`ci/prepare-p2play-embedded.sh`. Requires g++, pkg-config, timeout, ffmpeg, libmpv
and the development headers used by pPlay (SDL2/GLAD/FreeType/FFmpeg/libass/curl).
No Switch toolchain is used by these tests.

- `p2play_http_shutdown.cpp`: compiles the actual embedded HTTP implementation;
  tests idle listener, incomplete HTTP headers and incomplete HTTP body. The last
  two cases hang with the unpatched pinned e33886c core (external timeout required).
- `p2play_embedded_shutdown.cpp`: compiles the actual pPlay lifecycle wrapper and
  patched HTTP server, stubbing the torrent core C API. Tests shutdown with a live
  incomplete HTTP connection, shutdown during startup, repeated shutdown and
  rejection of activation after shutdown. Does not test libtorrent shutdown.
- `mpv_resync.cpp`: compiles the actual `mpv_props.cpp` against system libmpv,
  using a minimal headless Mpv constructor/destructor. Tests load/duration,
  repeated paused seeks, rapid pause/resume, file replacement and stop. A linker
  wrapper asserts zero synchronous property reads for pause/idle/EOF/duration/time.
  This is not an OSD rendering or hardware-decoding test.
- `mpv_keep_open_smoke.py`: pre-existing EOF/replay contract test.

The nested dependency patch is stored in pPlay's `ci/patches` and applied by the
prepare script; no separate push to P2Play is required. Its reverse-check makes
repeated preparation idempotent; incompatible core revisions fail explicitly.
