# OBS AirPlay Receiver (helper + thin plugin)

AirPlay screen mirroring into OBS Studio **32.2.2** on Apple Silicon. Protocol and decode run in **AirPlayReceiverHelper.app**; the OBS plugin only supervises the helper and pushes frames.

Not a Zoom AirHost clone. Zoom was audited read-only. See `docs/KNOWLEDGE.md`, `docs/ZOOM_AIRHOST_AUDIT.md`, and `docs/ARCHITECTURE_DECISION.md`.

## Requirements

- macOS 13+ Apple Silicon (`arm64`)
- OBS Studio 32.2.2 arm64 (`/Applications/OBS.app`)
- iPhone/iPad/Mac on the same LAN
- Built-in **AirPlay Receiver** can stay on. iPhone will show several names — pick the OBS source name (default **AirPlay Receiver**), not `MacBook Air`. Zoom Share iPhone at the same time only clutters the picker.

## Install

**On another Mac (no terminal):** double-click `dist/obs-airplay-0.2.0-macos-arm64.pkg` (rebuild with `./scripts/package_pkg.sh`). Quit OBS first. No admin password. Installs to `~/Library/Application Support/obs-studio/plugins/obs-airplay.plugin`. Does not touch `/Applications/OBS.app`.

This `.pkg` is **not notarized**. If macOS blocks it: Control-click → Open, or System Settings → Privacy & Security → Open Anyway. AirDrop/USB often skip that prompt; a download from the internet usually does not.

**This Mac, from the build tree:**

```bash
./scripts/install.sh
```

Same destination. Quit OBS first. No sudo.

## Use

1. OBS → Sources → **+** → **AirPlay Receiver**
2. Rename the source if you want a custom AirPlay name (default `AirPlay Receiver`, second source `AirPlay Receiver 2`)
3. On iPhone: Control Center → Screen Mirroring → that source name
4. Video + audio should appear on the source. Audio pad defaults to **−6 dB** (source properties). iPhone volume buttons do not change capture level — use this pad and the OBS mixer.

Killing the helper must not crash OBS; the plugin restarts it with backoff if “Restart helper on crash” is on.

## Uninstall

```bash
./scripts/uninstall.sh
```

## Build

See `docs/BUILD.md`.

## Versions (this tree)

| Piece | Pin |
|---|---|
| OBS | 32.2.2 |
| UxPlay | v1.73.6 `21eef8df25d91e12635c36d8176ad192725baca2` |
| FFmpeg (helper, bundled) | Homebrew 8.1.2_1 arm64 |
| OpenSSL (static into UxPlay) | 3.6.3 arm64 `/opt/homebrew` |
| libplist | 2.7.0 |
| fdk-aac | 2.0.3 |

2022 UxPlay pin `64a7dd0fa09aefd643bd895c437bba9573e13ac4` is kept only as git history in `vendor/obs-airplay-upstream`. **Shipping binary uses 1.73.6.**

## License

- Plugin/helper wrapper: same terms as upstream obs-airplay where applicable
- UxPlay: GPL-3.0 (`third_party/UxPlay/LICENSE`)
- fdk-aac: Fraunhofer FDK AAC license (bundled dylib)

## What is not done until you test

2h soak, reconnect loop, Wi-Fi toggle: see `docs/TEST_REPORT.md`. Lock/unlock and first mirror are confirmed.
