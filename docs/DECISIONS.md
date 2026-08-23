# Decisions

Canonical architecture write-up: `docs/ARCHITECTURE_DECISION.md`.

| Date | Decision | Why |
|---|---|---|
| 2026-08-23 | Plugin CPU crossfade (~300ms) between idle / live / pause | Hard cuts on connect/lock/unlock/disconnect. Fade is pixels only: snapshot previous BGRA, lerp in `video_tick`. Do not fade in the helper, do not fade audio, do not delay `airplay_status`. Skip fade on first paint, canvas resize, stub language refresh, and `Connecting` (already idle). |
| 2026-08-23 | Paused = `video_pause` only; do not infer lock from video stall | Screen Mirroring stops VCL on a static screen. Stall ≥2.5s + live `/feedback` looked like a missed `0x56` and showed the lock stub while the iPhone was unlocked. Last frame stays until real `0x56`/`0x5e` or `/feedback` death. |
| 2026-08-23 | Dead client = missed `/feedback` ≥8s → Discoverable, not Paused | UxPlay standalone resets after 15s of silence (`feedback_callback`). Our helper had no watchdog: `conn_feedback` was reused as a video-stall→Paused path, and it only runs when a heartbeat **arrives**. Wi-Fi off hangs TCP; last OBS frame stays forever. Restore heartbeat semantics. Video silence is not lock (see row above). Drop via `raop_remove_known_connections` from the helper loop, not from the mirror thread. |
| 2026-08-23 | **B: helper + thin OBS plugin**, not a monolith in `OBS.app` | Zoom AirHost is already a separate hardened process with its own FFmpeg. Decoder/protocol faults must not kill OBS. OBS 32.2.2 ships FFmpeg 7.x; helper ships its own dylibs via `@loader_path`. |
| 2026-08-23 | UxPlay **v1.73.6** `21eef8df25d91e12635c36d8176ad192725baca2` (FDH2) + local `3642329` | Maintained fork; 2022 pin kept only as baseline in `vendor/obs-airplay-upstream`. Local commit keeps SPS prepend when NTP after sleep differs. Do not push that patch to FDH2 unless asked. |
| 2026-08-23 | Transport: unix-domain SOCK_STREAM (priority 3) | IOSurface needs Mach bootstrap between two ad-hoc binaries under library validation. Shm ring next if socket copy shows in profiles. Protocol still has generation id + timestamps + bounded drop-oldest. |
| 2026-08-23 | Native VideoToolbox first, FFmpeg only inside helper | FFmpeg must not load into the OBS process. Plugin `otool -L` is `libobs` + `obs-frontend-api` + system. |
| 2026-08-23 | Install only to user plugins dir | Never copy into `/Applications/OBS.app`. No sudo. |
| 2026-08-23 | Ship an **unsigned** `.pkg` with `enable_currentUserHome` only | OBS 28+ loads `.plugin` only from `~/Library/.../plugins`. A system `/Library` payload would install and never load. No Apple Developer ID → no notarize; Gatekeeper is documented, not bypassed. |
| 2026-08-23 | Helper starts on **activate**, stops on **deactivate** | Source in an inactive scene must not advertise Bonjour. |
| 2026-08-23 | GET `/info` crash was NULL `audio_set_client_volume`, not mDNS conflict with the OS receiver | Stub callbacks; do not copy Zoom TXT. |
| 2026-08-23 | Idle stub + canvas letterbox in the **plugin**, not helper | Async `obs_source_output_video` already owns pixels. Helper is off on deactivate. Zoom PNG not used. |
| 2026-08-23 | Source size locked to OBS canvas; live frames contain-scaled with black bars | iPhone portrait vs Mac 16:9 must not jump scene-item bounds. Stop mirroring snaps back to stub at the same size, never last native WxH. |
| 2026-08-23 | `connected == Streaming \|\| Paused`; signal `airplay_status` on `obs_get_signal_handler()` | Lua/Advanced Scene Switcher hook. Handshake `Connecting` is not connected. iPhone lock is Paused (still connected). Do not persist `connected` in source settings. |
| 2026-08-23 | iOS lock ≠ lock screen; Paused shows a lock stub at last iPhone size | AirPlay mirroring does not send lock-screen pixels. `video_pause` → stub, keep VT session. Pause stub is not the connect instructions; it is letterboxed at last native WxH so the phone rectangle does not jump to canvas 16:9. Wake continues the GOP (P-frames). Resetting the decoder on pause broke resume. |
| 2026-08-23 | Identical SPS/PPS resend ≠ new VideoToolbox session | Unlock prepends the same SPS onto the next VCL. Recreating VT on every type 7/8 NAL emptied DPB; P-frames then failed until an IDR that Screen Mirroring may not send. Recreate only on byte change; then wait for IDR. UxPlay: do not drop that prepend when NTP of SPS ≠ NTP of the following packet. **Confirmed live 2026-08-23:** lock → stub, unlock → video. |
| 2026-08-23 | Helper POST_BUILD copies into the plugin bundle | `obs-airplay` MODULE POST_BUILD does not run when only the helper relinks. Without the helper copy step, `install.sh` shipped a stale decoder (unlock looked unchanged). Verify `strings` on the **bundled** helper, not `AirPlayReceiverHelper.app` at the build root. |
| 2026-08-23 | Knowledge dump lives in `docs/KNOWLEDGE.md` + `.cursor/rules/obs-airplay.mdc` | Lock/GOP/install traps were only in chat; next session must not rediscover them. |
| 2026-08-23 | AirPlay Bonjour name = OBS **source name** | Two sources can run at once; a module-wide Tools name collided. Rename the source → helper restarts with the new name (session drops). Stub language + scene automation stay module config (Tools window). |
| 2026-08-23 | Stub language + scene automation are **module** config (Tools window) | Cocoa `NSWindow`, not NSAlert. Canvas `ru`/`en` independent of OBS UI locale. `airplay_status` can switch OBS scenes if names are set. Source must be on both scenes. |
| 2026-08-23 | Do **not** follow iPhone/AirPlay volume for capture | Screen Mirroring usually does not send `SET_PARAMETER volume`; feature bit 3 stays off. Capture level = source pad `audio_gain_db` (default −6 dB) + OBS mixer. |

## Lua: `airplay_status`

```lua
local obs = obslua

function on_airplay_status(cd)
  local src = obs.calldata_source(cd, "source")
  local on = obs.calldata_bool(cd, "connected")
  -- switch to demo scene when on == true, back when false
end

function script_load(settings)
  local sh = obs.obs_get_signal_handler()
  obs.signal_handler_connect(sh, "airplay_status", on_airplay_status)
end

function script_unload()
  local sh = obs.obs_get_signal_handler()
  obs.signal_handler_disconnect(sh, "airplay_status", on_airplay_status)
end
```

Poll instead of signal:

```lua
local cd = obs.calldata_create()
obs.proc_handler_call(obs.obs_source_get_proc_handler(src), "get_airplay_status", cd)
local on = obs.calldata_bool(cd, "connected")
obs.calldata_destroy(cd)
```

