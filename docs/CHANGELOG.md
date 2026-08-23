# Changelog vs mika314/obs-airplay

- Replaced Coddle with CMake (OBS plugin template layout, no Qt/CI).
- Split process: `AirPlayReceiverHelper.app` owns UxPlay + decode; OBS plugin is supervisor + IPC ingest.
- UxPlay **shipping pin** v1.73.6 `21eef8df25d91e12635c36d8176ad192725baca2` (not the 2022 submodule).
- Adapted `raop_callbacks_t` to 1.73.6 (`raop_init` / `raop_init2` / `raop_start_httpd` / `video_decode_struct`).
- Video: VideoToolbox H.264/HEVC, FFmpeg fallback; audio: fdk-aac AAC-ELD.
- Unix-socket IPC with generation id; helper crash must not take down OBS.
- Self-contained bundle + ad-hoc codesign of nested dylibs (required on macOS 26).
- Install to user plugins dir only; no files inside `OBS.app`.
- Zoom AirHost read-only audit docs; architecture decision B.
