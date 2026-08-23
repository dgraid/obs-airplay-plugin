# OBS AirPlay Receiver (helper + thin plugin)

AirPlay screen mirroring into OBS Studio **32.2.2** on Apple Silicon. Protocol and decode run in **AirPlayReceiverHelper.app**; the OBS plugin only supervises the helper and pushes frames.

Not a Zoom AirHost clone. Zoom was audited read-only. See `docs/ZOOM_AIRHOST_AUDIT.md` and `docs/ARCHITECTURE_DECISION.md`.

## Requirements

- macOS 13+ Apple Silicon (`arm64`)
- OBS Studio 32.2.2 arm64 (`/Applications/OBS.app`)
- iPhone/iPad/Mac on the same LAN
- Built-in **AirPlay Receiver** can stay on. iPhone will show several names — pick **OBS AirPlay**, not `MacBook Air`. Zoom Share iPhone at the same time only clutters the picker.

## Install

```bash
./scripts/install.sh
```

Copies `build/obs-airplay.plugin` to:

`~/Library/Application Support/obs-studio/plugins/obs-airplay.plugin`

Quit OBS first. No sudo. Does not write into `/Applications/OBS.app`.

## Use

1. OBS → Sources → **+** → **AirPlay Receiver**
2. Set receiver name (default `OBS AirPlay`)
3. On iPhone: Control Center → Screen Mirroring → that name
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

Live Zoom AirHost `--live` audit, iPhone mirror loop, 2h soak: see `docs/TEST_REPORT.md`.
