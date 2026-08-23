# Changelog vs mika314/obs-airplay

## 2026-08-23 (idle stub / status / Tools)

- Canvas instruction is always Russian (not OBS UI locale): «Повтор экрана», «Нажмите».
- Idle: full-canvas instruction frame (CoreGraphics) instead of 16×16 empty source. Stop mirroring replaces the last frame with the stub.
- Live video is letterboxed into the OBS canvas size so iPhone vs Mac does not change source geometry.
- Connection status: global signal `airplay_status(ptr source, bool connected)` and source proc `get_airplay_status`. `connected` is Streaming only. Not saved in scene JSON.
- Tools → AirPlay Receiver: global receiver name (`obs-airplay.json`). Per-source `server_name` removed from properties (one-time migrate).
- Locale `en-US` + `ru-RU` for Tools/properties. Canvas stub copy is always Russian (OBS UI language ignored).

## 2026-08-23 (handshake crash)

- GET `/info` no longer SIGSEGV: stub `audio_set_client_volume` and the rest of UxPlay callbacks that the library calls without a NULL check.
- `_airplay._tcp` and `_raop._tcp` advertised on the **same** TCP port (UxPlay `uxplay.cpp`).
- Helper stderr is copied into the OBS log (`[obs-airplay] [helper] …`).
- Persistent random MAC stored in source settings (`device_mac`).
- Helper logs other `_airplay._tcp` names; they are not a protocol lock.

## 2026-08-23

- Helper launches on source **activate**, stops on **deactivate** (not at OBS start).
- Supervisor reaps helper via `waitpid` (macOS zombies still `kill(pid,0)`-alive).
- Supervisor retries spawn if the first helper start fails.
- Docs: `DECISIONS.md`, `PROJECT_CONTEXT.md`; UxPlay recorded as a git submodule pin.

- Replaced Coddle with CMake (OBS plugin template layout, no Qt/CI).
- Split process: `AirPlayReceiverHelper.app` owns UxPlay + decode; OBS plugin is supervisor + IPC ingest.
- UxPlay **shipping pin** v1.73.6 `21eef8df25d91e12635c36d8176ad192725baca2` (not the 2022 submodule).
- Adapted `raop_callbacks_t` to 1.73.6 (`raop_init` / `raop_init2` / `raop_start_httpd` / `video_decode_struct`).
- Video: VideoToolbox H.264/HEVC, FFmpeg fallback; audio: fdk-aac AAC-ELD.
- Unix-socket IPC with generation id; helper crash must not take down OBS.
- Self-contained bundle + ad-hoc codesign of nested dylibs (required on macOS 26).
- Install to user plugins dir only; no files inside `OBS.app`.
- Zoom AirHost read-only audit docs; architecture decision B.
