# Architecture decision: AirPlay receiver for OBS

Date: 2026-08-23  
Inputs: `docs/ZOOM_AIRHOST_AUDIT.md`, OBS 32.2.2 arm64 plugin ABI, UxPlay v1.73.6 (`21eef8df25d91e12635c36d8176ad192725baca2`). Operational dump: `docs/KNOWLEDGE.md`.

## Options

### A — Monolithic OBS plugin

Bonjour, RAOP/AirPlay, FairPlay-legacy pairing (UxPlay), decrypt, decode, and `obs_source_output_*` all run in `OBS.app`.

### B — Helper + thin plugin (default)

`AirPlayReceiverHelper.app` owns Bonjour, session, decrypt, decode.  
`obs-airplay.plugin` owns settings, process supervision, timestamped ingest, OBS output.

## Comparison

| Criterion | A | B |
|---|---|---|
| Blast radius of bad packets / decoder abort | Kills OBS | Kills helper; OBS stays |
| FFmpeg/OpenSSL collision with OBS | High (OBS ships FFmpeg 7.1.1) | Helper loads **its** dylibs via `@loader_path` |
| Reconnect / restart | Must re-init in-process | Bounded backoff, new generation id |
| Latency / A-V sync | Fewer hops | Extra IPC; timestamps on every frame |
| Packaging | One Mach-O | Helper.app nested in plugin Resources |
| Long-run leaks | Same heap as OBS | Helper can be recycled |
| OBS upgrades | Tight coupling | Plugin surface stays small |

## Decision

**B.** Zoom AirHost **Fact:** AirPlay is already a separate hardened process with its own FFmpeg. That is the only blast-radius evidence we need. A is rejected as the shipping shape “because fewer files”.

A would be revisited only if B cannot pass codesign/library validation **and** that is written here with measurements — not because CMake was easier.

## Data path

```text
iPhone/iPad/Mac
  -- Bonjour _airplay._tcp / _raop._tcp + AirPlay mirror -->
AirPlayReceiverHelper.app
  UxPlay 1.73.6 (pin SHA 21eef8d…)
  VideoToolbox H.264/HEVC (FFmpeg fallback in helper)
  fdk-aac / AudioConverter → PCM
  -- unix socket, monotonic timestamps, generation id -->
obs-airplay.plugin
  supervisor (start / stop / backoff)
  obs_source_output_video / obs_source_output_audio
OBS 32.2.2
```

## Transport choice (this build)

**Unix-domain SOCK_STREAM** with length-prefixed messages (priority 3 in the prompt).

Why not IOSurface first: needs a Mach bootstrap between two ad-hoc signed binaries; more failure modes under library validation. Shm ring is next if socket copy shows up in profiling.

The protocol still carries: monotonic ns, geometry, pixel format, sample rate/channels, duration, **generation** (drop stale sessions), bounded 8-frame queue with drop-oldest backpressure.

## Helper lifecycle

- Spawned when the OBS source becomes active, not at OBS launch.
- Graceful SIGTERM, SIGKILL after timeout.
- Restart: 0.5s, 1s, 2s, 4s, cap 8s; max 10 restarts / 30s then `failed`.
- Kill helper during streaming must not crash OBS (plugin read loop treats EOF as disconnected).
- OBS exit / source destroy: wait, then kill, unlink socket.

## What we will not do

- Copy Zoom identity, TXT, certificates, or binaries.
- Load helper dylibs into OBS.
- Advertise as Zoom or impersonate `us.zoom.airhost`.
