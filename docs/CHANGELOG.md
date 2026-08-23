# Changelog vs mika314/obs-airplay

## 2026-08-23 (dark Apple TV idle stub)

- Waiting / Stop Mirroring canvas: dark two-column Apple TV layout — AirPlay icon + wordmark, glass card with source name, Control Center path, 5 GHz Wi-Fi / Ethernet hint.
- Narrow canvases stack brand above the card. Pause stub (lock) is unchanged.
- Breaking: no.

## 2026-08-23 (Wi-Fi drop → connect stub)

- Helper watches iOS POST `/feedback`. Silence ≥8s while a session is open → Discoverable (connect instructions), decoder reset, HTTP connections dropped.
- Video stall ≥2.5s becomes Paused only if the heartbeat is still alive (real lock, missed `0x56`). Dead network is not lock.
- Breaking: no. Lock still stays `connected=true`. Wi-Fi off / vanished client now fires on-disconnect after ~8s.

## 2026-08-23 (pause stub at iPhone size)

- Paused (iPhone lock): separate stub (“зеркало на паузе” / unlock to resume), not the connect instructions.
- Pause card is rendered at last native iPhone WxH and letterboxed into the OBS canvas — same phone rectangle as live video. OBS source size stays canvas (scene items do not jump). Stop Mirroring still shows the full-canvas connect stub.
- Pause copy is centered; no “session still connected / Stop Mirroring” line.
- Breaking: no.

## 2026-08-23 (knowledge dump)

- Canonical plugin knowledge: `docs/KNOWLEDGE.md`. Agent always-on rule: `.cursor/rules/obs-airplay.mdc`.
- Lock/unlock GOP + stale-helper install trap recorded as confirmed (owner live test).
- Breaking: no.

## 2026-08-23 (unlock: identical SPS ≠ new decoder)

- VideoToolbox session is recreated only when SPS/PPS/VPS **bytes** change. Unlock `0x16` resends the same SPS prepended to a P-frame; treating that as a new decoder wiped DPB and dropped the GOP (Screen Mirroring rarely sends IDR).
- After a real parameter change, P-frames are ignored until IDR (H.264 type 5) / HEVC IRAP.
- UxPlay local patch: keep SPS prepend on the next VCL even if NTP timestamps differ after sleep.
- Decode-fail log includes first VCL NAL type, VT OSStatus, and whether the session was recreated.
- CMake copies the helper into the plugin bundle on helper relink (not only when the plugin MODULE rebuilds). Previously `install.sh` could ship a stale helper.
- Breaking: no.

## 2026-08-23 (iPhone lock pause)

- Helper honors UxPlay `video_pause` / `video_resume` (iOS lock = SPS/PPS `0x56`/`0x5e`). New IPC `State::Paused`.
- On lock: idle stub, not a frozen last frame. `connected` stays true — on-disconnect scene does not fire.
- Decoder session stays warm across lock/unlock. iPhone often sends P-frames on wake, not IDR; resetting VT on pause killed resume (FFmpeg `reference frames exceeds max`, then TCP drop).
- Per-frame VT miss does not permanently fall back to FFmpeg. `reset_session` only on flush/teardown.
- Stall fallback: if Streaming and no video for ≥2.5s (missed `0x56`), helper enters Paused without dropping the decoder.
- Breaking for scenes: no. Lock is still `connected=true`. Stop Mirroring is still disconnect.

## 2026-08-23 (source name = AirPlay name)

- Bonjour / Screen Mirroring name is the OBS source name (`AirPlay Receiver`, `AirPlay Receiver 2`, or whatever you rename it to). Tools no longer has a receiver-name field.
- Rename while live restarts that source's helper (AirPlay session drops). Idle stub quotes the same name.
- Breaking: old Tools name (`OBS AirPlay`) is ignored. Rename the source if you want that string back.

## 2026-08-23 (unsigned .pkg)

- `scripts/package_pkg.sh` builds `dist/obs-airplay-0.2.0-macos-arm64.pkg` (productbuild, currentUserHome only, hostArchitectures=arm64).
- Payload is `~/Library/Application Support/obs-studio/plugins/obs-airplay.plugin`. No sudo, not `/Library`, not `OBS.app`.
- preinstall aborts if OBS is running or the Mac is not arm64. postinstall strips quarantine and re-applies ad-hoc codesign.
- Not Developer ID / not notarized. Gatekeeper on a downloaded copy needs Open Anyway. Breaking: no.

## 2026-08-23 (audio pad)

- Source property `audio_gain_db` (−24…0 dB, default −6). PCM converted to float before OBS so the pad restores headroom; mixer fader is the live control.
- iPhone hardware volume during Screen Mirroring is not applied. `SET_PARAMETER volume` is logged and ignored.
- Breaking: no. Existing sources pick up the −6 dB default.

## 2026-08-23 (idle stub / status / Tools)

- Tools → AirPlay Receiver is a settings window (name, language, live status, scene on connect/disconnect). Not an NSAlert rename dialog.
- Idle: full-canvas instruction frame (CoreGraphics) instead of 16×16 empty source. Stop mirroring replaces the last frame with the stub.
- Live video is letterboxed into the OBS canvas size so iPhone vs Mac does not change source geometry.
- Connection status: global signal `airplay_status(ptr source, bool connected)` and source proc `get_airplay_status`. `connected` is Streaming **or** Paused. Not saved in scene JSON.
- Per-source `server_name` removed from properties (one-time migrate into module config).
- Locale `en-US` + `ru-RU` for Tools/properties.

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
