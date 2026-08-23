# Plugin knowledge (canonical)

Read this before changing AirPlay, decode, IPC, CMake, or install. Chat history is not the source of truth.

Companion tables: `docs/DECISIONS.md`, `docs/ARCHITECTURE_DECISION.md`, `docs/PROJECT_CONTEXT.md`, `docs/BUILD.md`, `docs/TEST_REPORT.md`.

## What this is

AirPlay **receiver** for OBS Studio **32.2.2** on Apple Silicon. iPhone/iPad/Mac Screen Mirroring is an OBS source (video + audio).

Not Zoom. `airhost.app` was audited read-only (`docs/ZOOM_AIRHOST_AUDIT.md`). Do not copy Zoom identity, TXT, certs, or binaries.

## Processes

| Binary | Owns |
|---|---|
| `obs-airplay.plugin` | Tools window, helper supervisor, unix-socket ingest, idle stub, canvas letterbox, ~300ms state crossfade, `obs_source_output_video/audio`, `airplay_status` |
| `AirPlayReceiverHelper.app` | Bonjour, UxPlay RAOP/AirPlay, decrypt, VideoToolbox (+ FFmpeg fallback), AAC-ELD |

Installed layout:

```
~/Library/Application Support/obs-studio/plugins/obs-airplay.plugin/
  Contents/MacOS/obs-airplay
  Contents/Resources/AirPlayReceiverHelper.app/
    Contents/MacOS/AirPlayReceiverHelper
    Contents/Frameworks/          # FFmpeg, fdk-aac, … @loader_path
```

Plugin Mach-O links `libobs` + `obs-frontend-api` + system only. Helper dylibs never load into OBS.

## Data path

```
iPhone -- Bonjour _airplay._tcp / _raop._tcp + mirror TCP -->
Helper (UxPlay → decrypt → VT H.264/HEVC → BGRA, fdk-aac → PCM)
  -- unix SOCK_STREAM /tmp/obs-airplay-<obs-pid>-<ptr>.sock -->
Plugin (generation id, drop-oldest ~8 frames) → OBS canvas / mixer
```

IPC: 48-byte header (`airplay_ipc::Header`, magic `APRC`, v1) + payload. Types: Hello, State, Video, Audio, Log. States: Starting, Discoverable, Connecting, **Streaming**, Disconnected, Failed, **Paused=7**.

Helper `send_state` skips duplicate states. `video_resume` does **not** set Streaming; the next successful decode does.

## Source / Bonjour / Tools

- AirPlay picker name = **OBS source name** (default `AirPlay Receiver`, second source `AirPlay Receiver 2`). Rename live → helper restarts, session drops.
- Tools → AirPlay Receiver: stub language `ru`/`en`, on-connect / on-disconnect scene names. Stored in module config `obs-airplay.json` (not source settings).
- `connected == Streaming || Paused`. Handshake `Connecting` is not connected. Stop Mirroring **or** Wi-Fi/client gone (`/feedback` silent ≥8s) = disconnect. iPhone lock = Paused (still connected) so on-disconnect scene must **not** fire.
- Signal: `airplay_status` on `obs_get_signal_handler()` (`ptr source`, `bool connected`). Proc: `get_airplay_status`.
- Source size (OBS) stays canvas. Live frames contain-scaled with black bars. Idle (no session) = connect instructions at canvas size. **Paused** = lock-specific stub at last iPhone native WxH, letterboxed into the same canvas (phone rectangle does not jump to 16:9). Never a frozen last frame. Visual idle/live/pause **crossfade ~300ms**; the inner letterbox rect **morphs** wide↔narrow (connect/disconnect and rotation). Status signal is immediate.
- Audio pad `audio_gain_db` default −6. iPhone volume is not capture level (`SET_PARAMETER volume` ignored; feature bit 3 off).
- Helper starts on source **activate**, stops on **deactivate**. Persistent random MAC in source settings `device_mac`.
- Restart backoff: 0.5s … cap 8s; max 10 / 30s then Failed. Kill helper ≠ kill OBS.

## iPhone lock / unlock (confirmed 2026-08-23)

iOS mirroring does **not** send lock-screen pixels. UxPlay (`raop_rtp_mirror.c`):

| packet[6] | Meaning | Helper |
|---|---|---|
| `0x56` / `0x5e` | sleep / stream stop | `video_pause` → IPC Paused → pause stub at last iPhone size |
| `0x16` / `0x1e` | wake | `video_resume` (log only). Next VCL should decode |

Unlock also sends unencrypted SPS/PPS (`0x16`) and UxPlay **prepends** them to the next encrypted VCL. Screen Mirroring uses a long GOP / intra refresh: wake is usually **P-frames**, not IDR. A static screen also stops sending VCL; that is **not** lock — keep the last decoded frame.

Paused is **only** `video_pause` (`0x56`/`0x5e`). Do not infer lock from video silence: a 2.5s stall fallback false-triggered the lock stub on idle home screen.

Wi-Fi / network drop: iOS stops `/feedback` (expected every ~2s). Helper has no UxPlay GLib watchdog; it counts silence itself. After **8s** without feedback while Connecting/Streaming/Paused → Discoverable (connect stub), `reset_session`, `raop_remove_known_connections`. Do **not** treat a dead TCP hang as Paused/lock. TCP keepalive can sit for minutes; `conn_destroy`/`conn_reset` often never fire.

**Owner-confirmed:** lock → stub; unlock → live video immediately; no Stop Mirroring.

## Decoder (do not “simplify”)

`src/helper/video_decoder.cpp`, VideoToolbox first.

1. Recreate VT only if SPS/PPS/VPS **bytes** actually change (or there is no session). Identical SPS on unlock must keep the warm DPB.
2. After a **real** parameter change: set `need_idr`; drop P-frames until H.264 type 5 / HEVC IRAP (16–21).
3. Do **not** `reset_session()` on pause/resume. Only `video_flush` / `video_reset` (teardown).
4. Per-frame VT miss (`!got_frame` / OSStatus) must not set `vt_failed` forever. `got_frame` prevents reusing a stale `last_bgra`.
5. Fail log (first 8): `video decode failed (len=… h265=… nal=… vt=… recre=…)`. `nal` = first VCL type, `vt` = OSStatus, `recre` = session rebuilt this packet.

Wrong (already burned): treat every NAL 7/8 as `params_changed` → `destroy_session` → empty DPB → P-frames fail until an IDR that may never come.

## UxPlay

- Upstream pin: **v1.73.6** `21eef8df25d91e12635c36d8176ad192725baca2` (FDH2). 2022 pin in `vendor/obs-airplay-upstream` is baseline only.
- **Local** submodule commit `3642329`: keep SPS prepend when NTP of the `0x16` packet ≠ NTP of the following VCL (sleep often desyncs them). Do not discard prepend.
- Patches stay in-tree. Do not fork the protocol or push this to FDH2 unless asked.
- Same TCP port for `_airplay._tcp` and `_raop._tcp`. GET `/info` needs stub UxPlay callbacks (`audio_set_client_volume` was NULL → SIGSEGV). Other receivers on LAN (Mac, Zoom, TV) coexist; picker must use the OBS source name.

## Build / install (stale-helper trap)

```bash
cmake --build build
# bundled helper MUST contain the new log format:
strings build/obs-airplay.plugin/Contents/Resources/AirPlayReceiverHelper.app/Contents/MacOS/AirPlayReceiverHelper \
  | grep 'nal=%d vt=%d recre=%d'
osascript -e 'tell application "OBS" to quit'
./scripts/install.sh    # fails if OBS still running
open -a OBS
```

- `cmake --build` linking **only** `AirPlayReceiverHelper` used to leave `obs-airplay.plugin` stale: POST_BUILD lived only on the plugin MODULE. Helper POST_BUILD now copies into the plugin bundle; `add_dependencies(obs-airplay AirPlayReceiverHelper)`.
- Proof of ship: mtime + `strings` on **installed** helper under `~/Library/.../plugins/obs-airplay.plugin/.../AirPlayReceiverHelper`, not just `build/AirPlayReceiverHelper.app`.
- Install dest: user plugins dir only. No sudo. Never write into `/Applications/OBS.app`.
- Unsigned `.pkg`: `./scripts/package_pkg.sh` → `dist/obs-airplay-0.2.0-macos-arm64.pkg` (`enable_currentUserHome`). Not notarized.
- arm64 only. Native Homebrew `/opt/homebrew`. No `/usr/local` Intel brew. No SIP/Gatekeeper bypass.

OBS log: `~/Library/Application Support/obs-studio/logs/`. Helper stderr is prefixed `[obs-airplay] [helper]`.

## Operator

Owner chats only. Agent writes code, builds, quits/reinstalls/relaunches OBS, runs checks, local-commits. Do not ask the owner to run terminal commands.

- Local git commit after substantial code/docs. Branch `master` unless asked otherwise.
- `git push` / GitHub / deploy only on an explicit owner phrase.
- Confirmation matrix (user rule 3): minor = do; usual = short plan then wait; architecture = wait for «делай».
- No unit ctest in this tree. After decode/IPC/plugin changes: build + install + confirm plugin load in the newest OBS log. Live lock/unlock needs the owner’s iPhone.
- Tests not run are **BLOCKED**, not pass (`docs/TEST_REPORT.md`).

## Do not

- Recreate VideoToolbox because SPS was resent.
- Reset the decoder on iOS lock/unlock.
- Treat lock as disconnect / fire on-disconnect scene.
- Show a frozen last frame on lock (stub).
- Feed P-frames into a brand-new VT session (wait for IDR).
- Drop UxPlay SPS prepend on NTP mismatch.
- Load FFmpeg into the OBS process.
- Copy Zoom TXT / impersonate `us.zoom.airhost`.
- Claim a test pass without evidence.

## Map

| Path | Role |
|---|---|
| `src/plugin/` | OBS module: `plugin.cpp` (incl. state crossfade), idle stub, Tools (`tools_dialog.mm`), `module_settings.*` |
| `src/helper/` | `main.cpp` (UxPlay callbacks, IPC), `video_decoder.*`, `audio_decoder.*` |
| `src/common/ipc.hpp` | Header + State enum |
| `third_party/UxPlay/` | Submodule; local prepend patch |
| `cmake/` | bundle Info.plist |
| `scripts/install.sh` `uninstall.sh` `package_pkg.sh` `bundle_dylibs.py` | ship |
| `data/locale/` | `en-US.ini` `ru-RU.ini` |
