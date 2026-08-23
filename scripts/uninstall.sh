#!/usr/bin/env bash
set -euo pipefail
DEST="$HOME/Library/Application Support/obs-studio/plugins/obs-airplay.plugin"
if pgrep -x OBS >/dev/null 2>&1; then
  echo "Quit OBS first." >&2
  exit 2
fi
rm -rf "$DEST"
pkgutil --forget dev.local.obs-airplay >/dev/null 2>&1 || true
echo "Removed $DEST"
