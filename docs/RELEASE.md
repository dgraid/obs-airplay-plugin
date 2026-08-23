# How to cut a public release

Maintainer machine, Apple Silicon, OBS 32.2.2 installed.

## Before tag

1. iPhone on the same Wi-Fi: first mirror, lock/unlock, helper kill, reconnect. 2h soak if claiming stability.
2. Replace README placeholders: Control Center picker + live preview (`docs/images/05-iphone-picker.png`, `docs/images/06-live-preview.png`).
3. `./scripts/package_pkg.sh`
4. `shasum -a 256 dist/obs-airplay-0.2.0-macos-arm64.pkg`

## GitHub (only after explicit «пуш»)

Needs OWNER/REPO from the maintainer.

```bash
git tag -a 0.2.0 -m "0.2.0"
git push origin master
git push origin 0.2.0
gh release create 0.2.0 \
  dist/obs-airplay-0.2.0-macos-arm64.pkg \
  --title "0.2.0" \
  --notes-file docs/CHANGELOG.md
```

Attach the SHA256 in the release body. Put OWNER/REPO into the README download badge.

## Forum / Reddit

Text: `docs/ANNOUNCE.md`. Do not submit until the maintainer says so. Forums will likely reject a vibe-coded plugin even with the disclaimer.

## Not in 0.2.0

Apple notarize, GitHub Actions macOS CI, Windows/Intel builds.
