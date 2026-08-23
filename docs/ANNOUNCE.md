# Announcement drafts (not posted)

Do not submit until the maintainer says so. English is the forum/Reddit language.

## OBS Forums resource (likely rejected)

**Title:** AirPlay Receiver for OBS

**Type:** OBS Studio Plugin  
**Platform:** macOS (Apple Silicon)  
**Minimum OBS:** 32.2.2  
**Source:** (GitHub URL after push)

**Description (paste as-is, then trim if needed):**

AirPlay screen mirroring from iPhone, iPad, or Mac into OBS Studio on Apple Silicon. Adds an **AirPlay Receiver** source. Video and audio. macOS 13+, OBS 32.2.2, arm64 only.

This is a fork/rewrite of [mika314/obs-airplay](https://github.com/mika314/obs-airplay), not an official OBS Project plugin. Protocol stack is [UxPlay 1.73.6](https://github.com/FDH2/UxPlay) (GPL-3.0) in a helper process. The OBS plugin only supervises the helper and ingests frames. Zoom AirHost was inspected read-only as a process-isolation reference. No Zoom code or assets.

**AI / vibe-coding (required by Forum Resource and IP Policy):** This plugin was vibe-coded in [Cursor](https://cursor.com) by the maintainer. Models used across the work: Cursor Auto, Composer, GPT-5.3 Codex, Grok 4.6. Code was reviewed and tested by a human on a real Mac. Treat it as experimental. Report crashes via GitHub Issues.

Download the `.pkg` from GitHub Releases, not from this attachment. The package is unsigned (no Apple Developer ID). After download: Control-click → Open, or System Settings → Privacy & Security → Open Anyway.

Installs to `~/Library/Application Support/obs-studio/plugins/`. Does not modify `OBS.app`.

**Limits:** no Windows, no Intel Mac, not notarized. 2h soak / reconnect still needs a real iPhone in the same Wi-Fi.

## Reddit `r/obs`

Title: [Plugin] AirPlay Receiver for OBS — iPhone Screen Mirroring on Apple Silicon (open source fork)

Body:

macOS arm64 plugin: iPhone/iPad/Mac Screen Mirroring as an OBS source (video + audio).

Fork/rewrite of mika314/obs-airplay. UxPlay 1.73.6 runs in a helper so a decoder crash should not kill OBS.

Unsigned .pkg, OBS 32.2.2, Apple Silicon only. Gatekeeper: Control-click → Open.

Built in Cursor (vibe-coded; Auto / Composer / GPT-5.3 Codex / Grok 4.6). Experimental. Issues on GitHub.

(link after push)

## Discord OBS (plugins / show-and-tell)

Short:

AirPlay Receiver for OBS (macOS Apple Silicon): Screen Mirroring → OBS source. Fork of mika314/obs-airplay, UxPlay in a helper process. Unsigned pkg. Vibe-coded in Cursor. GitHub: (url)
