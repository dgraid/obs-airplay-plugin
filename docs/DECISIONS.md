# Decisions

Canonical architecture write-up: `docs/ARCHITECTURE_DECISION.md`.

| Date | Decision | Why |
|---|---|---|
| 2026-08-23 | **B: helper + thin OBS plugin**, not a monolith in `OBS.app` | Zoom AirHost is already a separate hardened process with its own FFmpeg. Decoder/protocol faults must not kill OBS. OBS 32.2.2 ships FFmpeg 7.x; helper ships its own dylibs via `@loader_path`. |
| 2026-08-23 | UxPlay **v1.73.6** `21eef8df25d91e12635c36d8176ad192725baca2` (FDH2), not antimof 2022 | Maintained fork; 2022 pin kept only as baseline in `vendor/obs-airplay-upstream`. Shipping SHA is exact, not `latest`. |
| 2026-08-23 | Transport: unix-domain SOCK_STREAM (priority 3) | IOSurface needs Mach bootstrap between two ad-hoc binaries under library validation. Shm ring next if socket copy shows in profiles. Protocol still has generation id + timestamps + bounded drop-oldest. |
| 2026-08-23 | Native VideoToolbox first, FFmpeg only inside helper | FFmpeg must not load into the OBS process. Plugin `otool -L` is `libobs` + system only. |
| 2026-08-23 | Install only to user plugins dir | Never copy into `/Applications/OBS.app`. No sudo. |
| 2026-08-23 | Helper starts on **activate**, stops on **deactivate** | Source in an inactive scene must not advertise Bonjour. |
| 2026-08-23 | Zoom AirHost is read-only evidence, not a source | No copy of binaries, bundle ids, certs, TXT impersonation. |
