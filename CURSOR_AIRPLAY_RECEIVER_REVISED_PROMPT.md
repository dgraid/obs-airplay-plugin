# Cursor task: audit Zoom AirHost and build a stable AirPlay Receiver for OBS 32.2.2 on macOS arm64

Work directly on the target Apple Silicon Mac where OBS Studio and Zoom Workplace are installed. The objective is not merely to compile code. Produce a repeatably installable AirPlay Receiver source for OBS that survives real iPhone connections, disconnects, malformed input, and helper-process crashes.

## Target environment

- macOS on Apple Silicon (`arm64`)
- OBS Studio 32.2.2 arm64
- current Zoom Workplace and its bundled `airhost.app`
- current iOS/iPadOS
- AirPlay Screen Mirroring input from iPhone, iPad, or Mac
- video and synchronized audio as one OBS source

Starting references:

- `https://github.com/mika314/obs-airplay`
- `https://github.com/antimof/UxPlay`
- `https://github.com/obsproject/obs-plugintemplate`
- local script `audit_zoom_airhost.sh`

## Non-negotiable constraints

1. Work autonomously: inspect errors, patch, rebuild, install, read OBS logs, and continue until the acceptance criteria are met.
2. Do not report success because compilation passed. Success requires real AirPlay video and audio inside OBS.
3. Keep this a clean-room interoperability project. Do not copy Zoom binaries, proprietary resources, code, cryptographic material, bundle identifiers, certificates, or private symbols into the new project.
4. Do not modify, re-sign, inject into, debug-attach to, or bypass protections of Zoom's `airhost.app`.
5. Read-only inspection through `otool`, `nm`, `strings`, `codesign`, `plutil`, `lsof`, unified logs, Bonjour discovery, and observable network metadata is allowed.
6. Do not globally disable SIP, Gatekeeper, hardened runtime, library validation, or macOS security controls.
7. Do not modify `/Applications/OBS.app`. Install the finished plugin only into the user's OBS plugin directory.
8. Do not use `sudo` unless a specific unavoidable requirement is explained first.
9. Preserve all upstream licenses and record exact dependency commits.

## Phase 0: audit the current Zoom AirHost implementation

Before changing `obs-airplay`, establish how the current installed version of Zoom isolates and runs AirPlay.

Run:

```bash
chmod +x audit_zoom_airhost.sh
./audit_zoom_airhost.sh
```

Then ask the user to open a Zoom meeting and select:

`Share -> iPhone/iPad via AirPlay`

While the AirPlay instructions are visible and `airhost.app` is running, run:

```bash
./audit_zoom_airhost.sh --live
```

Create `docs/ZOOM_AIRHOST_AUDIT.md`. Separate every conclusion into:

- **Fact:** directly supported by current local output.
- **Inference:** likely explanation derived from several facts.
- **Unknown:** not established without prohibited or unnecessary reverse engineering.

The audit must answer:

1. Is AirHost a separate process from Zoom Workplace?
2. What launches it, and what is its parent process?
3. Which architectures are present?
4. Which media, crypto, network, and IPC libraries are linked?
5. Does the current version use FFmpeg, VideoToolbox, AudioToolbox, OpenSSL/BoringSSL, libplist, IOSurface, Metal, shared memory, XPC, or local sockets?
6. Which TCP and UDP ports are opened while sharing is active?
7. Which Bonjour service types and visible TXT properties are advertised?
8. Does AirHost render into its own window?
9. Is there evidence of direct IPC into Zoom, window capture, IOSurface sharing, or another transport?
10. What happens to Zoom when AirHost is terminated normally? Do not force-crash or inject into it.
11. Which architectural facts plausibly explain its reconnection stability and why a receiver failure does not take down the main Zoom process?

Do not assert that Zoom is based on UxPlay, RPiPlay, ShairPlay, or another project unless current binary evidence proves it. Similar dependencies are not proof of lineage.

## Architecture decision gate

After the audit, write `docs/ARCHITECTURE_DECISION.md` comparing two approaches:

### A. Monolithic OBS plugin

- AirPlay networking, protocol parsing, decryption, decoding, and OBS frame delivery all run inside the OBS process.

### B. Receiver helper plus thin OBS plugin

- A separate helper owns Bonjour, AirPlay session state, protocol parsing, decryption, and preferably media decoding.
- The OBS plugin owns source settings, helper supervision, timestamped media ingestion, and OBS output only.

Evaluate:

- blast radius of malformed packets and decoder failures;
- dependency collisions with OBS;
- reconnect behavior;
- restartability;
- latency;
- audio/video synchronization;
- packaging complexity;
- long-running memory behavior;
- maintainability across OBS upgrades.

Default to **helper plus thin plugin** unless measurements demonstrate that it is technically infeasible. The old monolithic design is not accepted merely because it requires fewer files.

## Preferred target architecture

```text
iPhone/iPad/Mac
    -> Bonjour + AirPlay
AirPlay Receiver Helper.app
    -> protocol parsing and session lifecycle
    -> H.264/H.265 and AAC/ALAC decode
    -> timestamped local media transport
OBS AirPlay Source plugin
    -> helper supervision
    -> video/audio clock alignment
    -> obs_source_output_video/audio
OBS Studio
```

The helper must be disposable: if it exits, OBS remains alive, the source shows a clear disconnected state, and the plugin may restart the helper with bounded backoff.

## Protocol layer

Use UxPlay as the starting protocol implementation unless the audit and build results justify another maintained open-source receiver.

Requirements:

1. Do not keep the 2022 UxPlay submodule silently.
2. First record the existing pinned SHA and establish a baseline.
3. Evaluate the current maintained UxPlay revision against current iOS.
4. Pin the selected revision to an exact commit SHA.
5. Document all local patches separately from upstream code.
6. Preserve license notices and confirm distribution obligations for the combined helper and plugin.
7. Never copy Zoom's AirPlay identity, private keys, certificates, or proprietary TXT values.

## Media decoding

Prefer native macOS decoding when practical:

- VideoToolbox for H.264/H.265;
- AudioToolbox for AAC/ALAC;
- CoreMedia timestamps;
- IOSurface/Metal where public APIs permit zero-copy transfer.

Use bundled FFmpeg only if native decoding cannot satisfy the protocol or synchronization requirements. If FFmpeg is used:

- pin its major version;
- bundle only required libraries;
- set correct `rpath` values;
- ensure the helper, not OBS, loads those private copies;
- verify with `otool -L` that OBS cannot accidentally resolve against them.

## Local media transport

Select the transport based on public APIs and measured behavior. Preferred order:

1. IOSurface-backed frames plus a small authenticated local control channel.
2. A bounded shared-memory ring buffer carrying timestamped NV12 video and PCM audio.
3. A local Unix-domain socket transport if throughput and copying remain acceptable.

Do not use NDI, RTMP, or an additional lossy encode/decode cycle for the final local transport. Do not depend on capturing a visible helper window as the final product unless the user explicitly accepts that limitation.

The transport must include:

- monotonic timestamps;
- width, height, pixel format, sample rate, channel count, and frame duration;
- bounded queues and backpressure;
- generation/session identifiers so stale frames from an earlier connection are discarded;
- clean handling of rotation and resolution changes;
- no unbounded memory growth.

## Build system

Replace Coddle with a reproducible CMake build based on the official OBS Plugin Template if the current build cannot meet these requirements.

Build requirements:

- Release arm64 output;
- OBS 32.2.2 headers and compatible ABI;
- Xcode/CMake versions recorded;
- no absolute user-specific paths;
- no symlinks into the build directory in the installed result;
- correct plugin bundle layout;
- helper embedded in a predictable private location;
- ad-hoc local code signing where required;
- deterministic install and uninstall scripts.

Expected deliverables:

```text
obs-airplay.plugin/
  Contents/
    Info.plist
    MacOS/obs-airplay
    Resources/
      AirPlayReceiverHelper.app/
```

If macOS code-signing rules require a different valid bundle layout, document and implement it rather than forcing this example literally.

## Helper supervision and failure containment

Implement:

- launch on source activation, not on every OBS startup unless configured;
- graceful shutdown request;
- forced termination only after a timeout;
- bounded restart backoff;
- maximum restart rate;
- distinct states: starting, discoverable, connecting, streaming, disconnected, failed;
- useful logs without secrets or packet payloads;
- cleanup when the source is removed or OBS exits;
- no orphan helper processes.

Killing the helper during an active test must not crash or freeze OBS.

## Bonjour and network behavior

Compare the new receiver's advertisement and ports with both:

- the AirPlay protocol requirements implemented by the selected open-source stack;
- the observable Zoom AirHost audit.

Use the comparison to identify missing compatibility fields, not to impersonate Zoom.

Account for:

- built-in macOS AirPlay Receiver conflicts;
- Zoom AirHost conflicts;
- VPN and multiple network interfaces;
- Wi-Fi reconnects;
- IPv4/IPv6;
- mDNS service withdrawal and re-advertisement;
- randomized but stable per-source identity where protocol-compatible.

## OBS source requirements

Expose settings for:

- receiver name;
- maximum resolution;
- maximum FPS;
- audio enabled;
- preferred network interface or automatic selection;
- helper auto-restart;
- low-latency versus resilient buffering preset.

The source must provide:

- decoded video;
- synchronized audio in the OBS mixer;
- a transparent or configurable placeholder while disconnected;
- correct rotation and aspect ratio;
- clean source duplication and deletion behavior.

## Testing sequence

Run and document all tests:

1. Plugin loads in OBS 32.2.2 arm64 without missing libraries or symbols.
2. Source can be created and deleted repeatedly.
3. Receiver appears in iPhone Screen Mirroring.
4. First connection produces video.
5. Audio reaches the OBS mixer.
6. Portrait/landscape rotation works.
7. Resolution changes work without recreating the source.
8. Disconnect and reconnect work at least 20 times.
9. Disable and re-enable iPhone Wi-Fi during a session.
10. Lock and unlock the iPhone.
11. Terminate only the helper and verify OBS survives and recovers.
12. Run a continuous stream for at least two hours while monitoring CPU and memory.
13. Close OBS during an active connection and confirm no orphan helper remains.
14. Relaunch OBS and reconnect without deleting the source.
15. Verify behavior when Zoom AirHost or the macOS AirPlay Receiver already owns conflicting services or ports.

Do not mark unexecuted tests as passed. Label them `BLOCKED` with the exact manual action required from the user.

## Packaging

Produce:

- installable `obs-airplay.plugin`;
- ZIP archive;
- `install.sh` and `uninstall.sh` limited to this plugin's exact paths;
- `README.md`;
- `BUILD.md`;
- `docs/ZOOM_AIRHOST_AUDIT.md`;
- `docs/ARCHITECTURE_DECISION.md`;
- `docs/DEPENDENCIES.md` with exact SHAs and licenses;
- `docs/TEST_REPORT.md` with evidence and pass/fail status;
- changelog of modifications from upstream `mika314/obs-airplay`.

Before packaging, verify:

```bash
file <plugin-binary>
file <helper-binary>
otool -L <plugin-binary>
otool -L <helper-binary>
codesign --verify --deep --strict --verbose=4 <bundle>
```

The final installation must not reference Homebrew libraries or files in the build directory unless that dependency is explicitly declared as a runtime requirement. Prefer a self-contained bundle.

## Acceptance criteria

The task is complete only when:

- the current Zoom AirHost audit is written with facts separated from inference;
- the architecture decision is evidence-based;
- OBS 32.2.2 arm64 starts normally with the plugin installed;
- a current iPhone discovers the receiver;
- real video and audio appear in OBS;
- repeated reconnects work;
- helper failure does not terminate OBS;
- no orphan process remains after source deletion or OBS exit;
- the installed bundle has valid paths, architecture, and signature;
- the ZIP installs on the same target configuration without the build tree;
- all remaining risks and blocked tests are stated explicitly.

In the final report, provide exact paths to the bundle and ZIP, selected dependency SHAs, measured latency/CPU/memory, completed tests, blocked tests, and unresolved limitations. Do not hide failures behind phrases such as "should work" or "implementation complete."
