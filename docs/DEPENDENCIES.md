# Pinned dependencies

Recorded on the build machine 2026-08-23.

| Name | Version / SHA | Role | License |
|---|---|---|---|
| OBS Studio | 32.2.2 arm64 `/Applications/OBS.app` | host | GPL-2.0 |
| obs-studio sources | tag 32.2.2 | headers only | GPL-2.0 |
| UxPlay | **v1.73.6** `21eef8df25d91e12635c36d8176ad192725baca2` + local `3642329` (SPS prepend keep) | AirPlay/RAOP in helper | GPL-3.0 |
| UxPlay 2022 (not shipped) | `64a7dd0fa09aefd643bd895c437bba9573e13ac4` | baseline only | GPL-3.0 |
| obs-airplay upstream | mika314/obs-airplay (submodule tree in vendor/) | original plugin logic | see vendor LICENSE |
| FFmpeg | 8.1.2_1 arm64 Homebrew | helper decode fallback | LGPL/GPL |
| fdk-aac | 2.0.3 | AAC-ELD | FDK AAC |
| OpenSSL | 3.6.3 arm64 | UxPlay crypto (static libcrypto) | Apache-2.0 |
| libplist | 2.7.0 | UxPlay | LGPL-2.1 |
| SIMDe | 0.8.2 | OBS headers on arm64 | MIT |
| CMake | 4.4.0 | build | BSD |
| Ninja | 1.13.2 | build | Apache-2.0 |
| Apple clang | 17.0.0 | build | Apple |
| VideoToolbox / AudioToolbox | macOS 26.6 SDK | preferred decode | Apple |

Helper dylibs are copied into `AirPlayReceiverHelper.app/Contents/Frameworks` with `@loader_path`. Plugin Mach-O links only `libobs.framework` via `@rpath` (OBS app).
