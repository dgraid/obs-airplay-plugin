# Project context

Canonical operator dump (lock/unlock, decoder, install traps): `docs/KNOWLEDGE.md`.

## What this is

AirPlay **receiver** for OBS Studio 32.2.2 on Apple Silicon. iPhone/iPad/Mac Screen Mirroring appears as an OBS source with video + audio.

Not a Zoom product. Zoom `airhost.app` was audited read-only as a process-isolation reference.

## Entities

| Name | Role |
|---|---|
| `obs-airplay.plugin` | Thin OBS module: Tools settings, helper supervisor, IPC ingest, idle stub, canvas letterbox, `obs_source_output_video/audio`, `airplay_status` signal |
| `AirPlayReceiverHelper.app` | Nested helper: Bonjour, RAOP/AirPlay (UxPlay), decrypt, decode |
| IPC | 48-byte header + payload on a unix socket under `/tmp/obs-airplay-<pid>-<ptr>.sock` |
| UxPlay 1.73.6 | Protocol stack, compiled static into the helper only |

## Layout (installed)

```
~/Library/Application Support/obs-studio/plugins/obs-airplay.plugin/
  Contents/MacOS/obs-airplay
  Contents/Resources/AirPlayReceiverHelper.app/
    Contents/MacOS/AirPlayReceiverHelper
    Contents/Frameworks/   # FFmpeg, fdk-aac, … @loader_path
```

## Constraints

- macOS 13+, **arm64 only**. Intel Homebrew `/usr/local` is forbidden for this build.
- Do not disable SIP / Gatekeeper / hardened runtime.
- Do not write into `/Applications/OBS.app` except the agreed OBS 32.2.2 app itself.
- Success is load + discovery + real A/V + reconnect + helper death ≠ OBS death. Compile is not success.
- Tests not run are **BLOCKED**, not pass. See `docs/TEST_REPORT.md`.

## Idle / size / status

- No AirPlay session: source renders a full-canvas instruction stub (OBS canvas size). Last mirror frame is discarded on stop **and on iPhone lock** (`Paused`).
- Streaming: helper BGRA is contain-scaled into the same canvas (black bars).
- `Paused`: session still up (iPhone lock via `0x56`/`0x5e` only). Lock-specific stub at last iPhone size, letterboxed into the canvas. A static screen keeps the last frame — video silence is not lock. Identical SPS/PPS on wake does **not** recreate VideoToolbox. `connected` remains true so on-disconnect scene automation does not fire. Unlock / next decoded frame returns to Streaming. Wi-Fi off / vanished client: no `/feedback` for ≥8s → Discoverable (connect stub), `connected=false`.
- `connected` is true in `Streaming` **or** `Paused`. Lua listens to `airplay_status` (see `docs/DECISIONS.md`). iOS does not send lock-screen frames over mirroring.
- AirPlay Bonjour name = OBS source name (rename the source to rename the receiver). Stub language and scene automation: Tools → AirPlay Receiver. Stored in `obs-airplay.json`.
- Audio: AAC-ELD → float, pad `audio_gain_db` (default −6 dB). iPhone volume buttons are not capture level; ride the OBS mixer.

## Status (2026-08-23)

Shippable **bundle** exists (arm64, rpath, ad-hoc sign). Unsigned **`.pkg`** via `scripts/package_pkg.sh` → `dist/obs-airplay-0.2.0-macos-arm64.pkg` (currentUserHome, no sudo). Plugin **loads** in OBS 32.2.2. Live Screen Mirroring works. **iPhone lock → stub, unlock → live video** confirmed (no Stop Mirroring). Notarize is not in this tree.

Still **BLOCKED** without the owner: 2h soak, reconnect ≥20, Wi-Fi toggle, create/delete in the UI, Zoom `--live` port/TXT re-check. Kill-helper-while-streaming already PASS (`docs/TEST_REPORT.md`).
