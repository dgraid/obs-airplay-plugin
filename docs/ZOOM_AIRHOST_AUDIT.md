# Zoom AirHost audit (read-only)

Date: 2026-08-23  
Host: macOS 26.6 arm64 (Build 25G5065a), Apple M4  
Zoom Workplace: 6.7.7 (76486), bundle `us.zoom.xos`  
AirHost: `/Applications/zoom.us.app/Contents/Frameworks/airhost.app`  
Method: `otool`, `nm`, `strings`, `codesign`, `plutil`, `file`/`lipo`. No attach, inject, re-sign, or copy of Zoom binaries.

Each finding is tagged **Fact** / **Inference** / **Unknown**.

## 1. Separate process?

**Fact:** `airhost` is a nested `.app` with its own Mach-O executable, bundle id `us.zoom.airhost`, and `LSUIElement=true`. It is not the `zoom.us` main binary.

**Inference:** Zoom launches it as a helper for AirPlay receive during “Share iPhone/iPad”.

## 2. Who launches it? Parent?

**Fact (idle):** no `airhost` process while Zoom is not sharing.

**Unknown (live):** Zoom meeting + Share → iPhone/iPad via AirPlay was not running during this pass. Re-run `scripts/audit_zoom_airhost.sh --live` while the AirPlay instructions are on screen. Expected Fact: `ppid` of `airhost` is a Zoom process.

## 3. Architectures?

**Fact:** thin **arm64** (not a fat binary). Zoom 6.7.7 on this Mac is Apple Silicon-native.

## 4. Linked libraries (media / crypto / net / IPC)

**Fact:** `otool -L` on `airhost`:

| Kind | Libraries |
|---|---|
| FFmpeg 5.x (bundled) | `@rpath/libavutil.57.dylib`, `libavformat.59.dylib`, `libavcodec.59.dylib`, `libswscale.6.dylib`, `libswresample.4.dylib` |
| TLS | `@rpath/libcrypto.dylib`, `@rpath/libssl.dylib` (OpenSSL 3.0 ABI) |
| Zoom private | `@rpath/libzContext.dylib`, `util.framework`, `nydus.framework`, `tp.framework` |
| Apple media | CoreMedia, VideoToolbox (weak), AudioToolbox, CoreAudio |
| Apple net | GSS, Kerberos, CoreWLAN, SystemConfiguration |
| UI | Cocoa, AppKit, CoreGraphics, ImageIO (`LSUIElement` still links AppKit) |

`LC_RPATH`: `@loader_path/../Frameworks` and `@loader_path/../../../` (parent Zoom `Frameworks`).

Nested copies inside `airhost.app/Contents/Frameworks/`: FFmpeg 5 dylibs + `libzContext.dylib`. OpenSSL/util/nydus/tp come from the parent Zoom app via rpath.

## 5. FFmpeg, VT, AT, OpenSSL, plist, IOSurface, Metal, shm, XPC, sockets?

**Fact:**

- FFmpeg: yes (linked + nested 5.x dylibs).
- VideoToolbox / AudioToolbox / CoreMedia: yes (VT weak).
- OpenSSL 3: yes (`libcrypto`/`libssl`).
- `libplist`: **not** in `otool -L`.
- `nm -u`: `DNSServiceRegister`/`Browse`/`Resolve` (Bonjour), `AudioConverter*`, `mmap`, `shm_open`/`shm_unlink`.
- `nm -u` has **no** `IOSurface`, `xpc_`, `VTDecompressSession`, `CVPixelBuffer` symbols.

**Inference:** media decode likely FFmpeg + AudioConverter; VT may be used weakly or via other frameworks. `shm_open` implies POSIX shared memory somewhere (not proof of IOSurface). Strings contain `IOSurface` / `_ioSurface` / `setIOSurface:` — Objective-C selectors exist; that is **not** proof of the transport used with Zoom.exe.

**Unknown:** whether frames go to Zoom via shm, IOSurface, Mach port, or window capture. Not established without prohibited tracing.

## 6. TCP/UDP ports while sharing?

**Unknown:** live `lsof` not collected (airhost not running). Idle has no listen sockets.

## 7. Bonjour types and TXT?

**Fact (binary strings):** service type names `_airplay._tcp` and `_raop._tcp`, plus `txtAirPlay`, `BonjourHandler`.

**Unknown:** advertised instance name and TXT keys while live. Must be captured with `dns-sd -B` during `--live`.

## 8. Own window?

**Fact:** `LSUIElement=true` (no Dock icon). NIB `MainMenu`. PNG names `Tap Screen Mirroring.png`.

**Inference:** likely a small instruction overlay, not a full capture surface for Zoom’s gallery.

**Unknown:** whether Zoom composites that window (window capture) vs receiving buffers.

## 9. IPC into Zoom?

**Fact:** `shm_open`/`mmap` imported; rpath into parent Zoom Frameworks; private `nydus`/`tp`/`util` frameworks.

**Unknown:** XPC service names, shared-memory layout, IOSurface handshake. Not claimed.

## 10. What happens if AirHost exits normally?

**Unknown:** not exercised. Plan forbids force-crash/inject. Next live pass: Stop share (not `kill -9`) and record whether Zoom survives.

## 11. Why reconnect stability / blast radius?

**Fact:** AirPlay stack is a **separate signed process** (`us.zoom.airhost`, Hardened Runtime `flags=0x10000`, Developer ID Zoom). FFmpeg is **inside the helper**, not in `zoom.us`.

**Inference:** a decoder/protocol fault in AirHost can take down the helper without taking down the meeting process — this is the blast-radius model we copy. Reconnect can re-exec the helper.

**Unknown:** Zoom’s exact backoff / restart policy.

## Lineage (UxPlay / RPiPlay / Shairplay)

**Fact:** no imported symbols or unambiguous strings naming UxPlay, RPiPlay, or Shairplay were required to explain the binary.

**Fact:** strings include Apple-style `MediaControlServerConnection`, `FairPlayHWInfo_`, `FPSAPContextOpaque_`, `com.apple.fairplayd.versioned`.

**Inference:** AirPlay implementation is Zoom’s own (or another closed stack) using FairPlay/Apple media control structures — **not** evidence of UxPlay lineage. Shared libraries (FFmpeg, OpenSSL, Bonjour) are ubiquitous and prove nothing.

## Live pass

**Status: BLOCKED** — user action: start a Zoom meeting, Share → iPhone/iPad via AirPlay, leave the instruction UI up, then:

```bash
./scripts/audit_zoom_airhost.sh --live
```

Idle transcript: captured 2026-08-23 on this machine.
