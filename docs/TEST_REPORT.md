# Test report

Date: 2026-08-23. Host: macOS 26.6 arm64, OBS 32.2.2.

| ID | Test | Result | Evidence |
|---|---|---|---|
| 1 | Plugin Mach-O arm64, links `@rpath/libobs.framework` | **PASS** | `file` + `otool -L` on `obs-airplay.plugin` |
| 2 | Helper arm64, no Homebrew install names | **PASS** | `otool -L` → `@loader_path/../Frameworks/...` |
| 3 | `codesign --verify --deep --strict` | **PASS** | after signing nested dylibs |
| 4 | Helper connects to unix socket, sends State | **PASS** | smoke: 48-byte IPC header + State payload |
| 5 | Bonjour `_airplay._tcp` name | **PASS** | `dns-sd -B`: `OBS-AirPlay-Smoke` |
| 6 | Plugin loads in OBS 32.2.2 | **PASS** | log: `[obs-airplay] loaded (helper+plugin, UxPlay 1.73.6)` |
| 6b | Create/delete source repeatedly | **BLOCKED** | needs GUI (or obs-websocket). Do **not** invent a pass. |
| 7 | iPhone sees receiver | **PASS** | user picked `OBS AirPlay` in Control Center (then handshake crashed; that crash is fixed) |
| 8 | First connection video | **PASS** | Owner live Screen Mirroring 2026-08-23; OBS log `state streaming` (e.g. 16:50:25 in `2026-08-23 16-49-24.txt`) |
| 9 | Audio in mixer | **BLOCKED** | needs explicit owner check of the OBS mixer while mirroring |
| 9b | Audio pad −6 dB: int16 full-scale → float ≈ 0.501 | **PASS** | `audio_gain_lin_from_db(-6)` → 0.501187 |
| 10 | Rotate / resolution change | **BLOCKED** | needs 8 |
| 11 | Disconnect/reconnect ≥20 | **BLOCKED** | needs 8 |
| 12 | Wi-Fi toggle | **BLOCKED** | Owner 2026-08-23: off-Wi-Fi left last frame / did not return to connect stub (missing `/feedback` watchdog). Fix shipped same day; re-check: off Wi-Fi → connect window in ~8s, lock still pause stub. |
| 13 | Lock iPhone | **PASS** | Owner 2026-08-23 after helper 16:54 install: lock → stub, unlock → live video, no Stop Mirroring. Prior fails were a stale helper (16:36) still in the plugin bundle. |
| 14 | Kill helper, OBS stays | **PASS** | `kill -9` helper 28789; OBS pid 28640 alive; helper 28938 gen=3 discoverable |
| 15 | 2h soak CPU/RSS | **BLOCKED** | leave OBS+mirror running 2h |
| 16 | Quit OBS, no orphan helper | **PASS** | after OBS quit (pre-reinstall): `pgrep AirPlayReceiverHelper` empty |
| 17 | Relaunch OBS, reconnect without deleting source | **BLOCKED** | |
| 18 | Conflict vs macOS AirPlay Receiver / Zoom AirHost | **PASS** (coexist) | **Fact:** OS `MacBook Air (2)`, LG TV, `Зал` advertised at once with OBS. Not a bind/mDNS exclusive. Pick the OBS name. |
| 19 | Zoom AirHost idle audit | **PASS** | `docs/ZOOM_AIRHOST_AUDIT.md` |
| 20 | Zoom AirHost live ports/TXT | **PASS** | parent=`zoom.us`; TCP 50000+8888; TXT `model=AppleTV3,2` `srcvers=220.68`. Do **not** copy TXT. |

Unexecuted tests are **BLOCKED**, not pass.
