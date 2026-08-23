# Test report

Date: 2026-08-23. Host: macOS 26.6 arm64, OBS 32.2.2.

| ID | Test | Result | Evidence |
|---|---|---|---|
| 1 | Plugin Mach-O arm64, links `@rpath/libobs.framework` | **PASS** | `file` + `otool -L` on `obs-airplay.plugin` |
| 2 | Helper arm64, no Homebrew install names | **PASS** | `otool -L` → `@loader_path/../Frameworks/...` |
| 3 | `codesign --verify --deep --strict` | **PASS** | after signing nested dylibs |
| 4 | Helper connects to unix socket, sends State | **PASS** | smoke: accepted, 52+ byte headers |
| 5 | Bonjour `_airplay._tcp` name | **PASS** | `dns-sd -B`: `OBS-AirPlay-Smoke` |
| 6 | Plugin loads in OBS 32.2.2 | **PASS** | log: `[obs-airplay] loaded (helper+plugin, UxPlay 1.73.6)` |
| 7 | iPhone sees receiver | **BLOCKED** | turn off macOS AirPlay Receiver, then Control Center → Screen Mirroring |
| 8 | First connection video | **BLOCKED** | needs 7 |
| 9 | Audio in mixer | **BLOCKED** | needs 7 |
| 10 | Rotate / resolution change | **BLOCKED** | needs 7 |
| 11 | Disconnect/reconnect ≥20 | **BLOCKED** | needs 7 |
| 12 | Wi-Fi toggle | **BLOCKED** | needs 7 |
| 13 | Lock iPhone | **BLOCKED** | needs 7 |
| 14 | Kill helper, OBS stays | **BLOCKED** | after source exists: `killall AirPlayReceiverHelper` |
| 15 | 2h soak CPU/RSS | **BLOCKED** | leave OBS+mirror running 2h |
| 16 | Quit OBS, no orphan helper | **BLOCKED** | `pgrep AirPlayReceiverHelper` after quit |
| 17 | Relaunch OBS, reconnect without deleting source | **BLOCKED** | |
| 18 | Conflict vs macOS AirPlay Receiver / Zoom AirHost | **BLOCKED** | live Zoom `--live` audit also pending |
| 19 | Zoom AirHost idle audit | **PASS** | `docs/ZOOM_AIRHOST_AUDIT.md` |
| 20 | Zoom AirHost live ports/TXT | **BLOCKED** | `./scripts/audit_zoom_airhost.sh --live` during Share → iPhone |

Unexecuted tests are **BLOCKED**, not pass.
