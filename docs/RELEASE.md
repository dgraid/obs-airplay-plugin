# How to cut a public release

Maintainer machine, Apple Silicon, OBS 32.2.2 installed.

Owner phrase **«Отгрузи на гитхаб»** (and the same intent in English) means a **full GitHub release**, not a bare `git push`:

1. Bump `project(obs-airplay VERSION …)` in `CMakeLists.txt`.
2. Rebuild the plugin bundle (`cmake --build build`).
3. `./scripts/package_pkg.sh` → `dist/obs-airplay-<version>-macos-arm64.pkg`.
4. `shasum -a 256` next to the pkg.
5. Update README download links, CHANGELOG, this file’s example tag, `docs/PROJECT_CONTEXT.md`.
6. Local commit, annotated tag, `git push origin master` **and** the tag.
7. `gh release create` with the `.pkg` + `.sha256`.

A commit that is already on `origin/master` without a new tag/asset is **not** shipped.

## Before tag

1. iPhone on the same Wi-Fi: first mirror, lock/unlock, helper kill, reconnect. 2h soak if claiming stability.
2. Replace README placeholders: Control Center picker + live preview (`docs/images/05-iphone-picker.png`, `docs/images/06-live-preview.png`).
3. `./scripts/package_pkg.sh`
4. `shasum -a 256 dist/obs-airplay-<version>-macos-arm64.pkg`

## GitHub (only after explicit «Отгрузи на гитхаб» / «пуш»)

```bash
git tag -a 0.2.2 -m "0.2.2"
git push origin master
git push origin 0.2.2
gh release create 0.2.2 \
  dist/obs-airplay-0.2.2-macos-arm64.pkg \
  dist/obs-airplay-0.2.2-macos-arm64.pkg.sha256 \
  --title "0.2.2 — AirPlay Receiver for OBS (macOS Apple Silicon)" \
  --notes-file docs/CHANGELOG.md
```

Attach the SHA256 in the release body. Put OWNER/REPO into the README download badge.

This repo has no GitHub Actions. There is no Security CI to wait on.

## Forum / Reddit

Text: `docs/ANNOUNCE.md`. Do not submit until the maintainer says so. Forums will likely reject a vibe-coded plugin even with the disclaimer.

## Not in this tree

Apple notarize, GitHub Actions macOS CI, Windows/Intel builds.
