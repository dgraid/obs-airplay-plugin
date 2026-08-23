# Build (macOS arm64)

Native toolchain only (`/opt/homebrew`). Do not use `/usr/local` Intel Homebrew.

```bash
eval "$(/opt/homebrew/bin/brew shellenv)"
brew install cmake ninja pkg-config openssl@3 libplist fdk-aac ffmpeg simde
export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:/opt/homebrew/opt/openssl@3/lib/pkgconfig"

cmake -S . -B build -G Ninja \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build
```

Output: `build/obs-airplay.plugin`

Helper relink copies into that bundle (CMake POST_BUILD on `AirPlayReceiverHelper`). After a decoder/UxPlay change, confirm the **bundled** binary, not `build/AirPlayReceiverHelper.app` alone:

```bash
strings build/obs-airplay.plugin/Contents/Resources/AirPlayReceiverHelper.app/Contents/MacOS/AirPlayReceiverHelper \
  | grep 'nal=%d vt=%d recre=%d'
```

Then quit OBS and `./scripts/install.sh`. Check the same `strings` under `~/Library/Application Support/obs-studio/plugins/obs-airplay.plugin/...`.

Installer `.pkg` (unsigned, current-user home, arm64):

```bash
./scripts/package_pkg.sh
```

Writes `dist/obs-airplay-0.2.0-macos-arm64.pkg`. Needs the bundle already built. Not notarized — see README Gatekeeper notes.

Headers: `vendor/obs-studio-32.2.2/libobs` (OBS 32.2.2 tag). Link: `/Applications/OBS.app/Contents/Frameworks/libobs.framework`.

UxPlay is compiled as static `airplay` from `third_party/UxPlay` (no GStreamer). Helper bundles FFmpeg/fdk-aac dylibs and is ad-hoc signed.

Install: `./scripts/install.sh`
