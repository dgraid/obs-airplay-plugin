#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${1:-$ROOT/build/obs-airplay.plugin}"
DEST="$HOME/Library/Application Support/obs-studio/plugins/obs-airplay.plugin"

if [[ "$(uname -m)" != "arm64" ]]; then
  echo "This build is arm64-only. uname -m=$(uname -m)" >&2
  exit 1
fi
if [[ ! -d "$SRC" ]]; then
  echo "Missing plugin bundle: $SRC" >&2
  exit 1
fi

if pgrep -x OBS >/dev/null 2>&1; then
  echo "OBS is running. Quit OBS, then re-run $0"
  exit 2
fi

mkdir -p "$(dirname "$DEST")"
rm -rf "$DEST"
ditto "$SRC" "$DEST"
codesign --force --deep --sign - --timestamp=none "$DEST" >/dev/null
echo "Installed $DEST"
echo "Open OBS → Sources → + → AirPlay Receiver"
echo "If iPhone cannot see it: System Settings → General → AirDrop & Handoff → AirPlay Receiver → Off"
