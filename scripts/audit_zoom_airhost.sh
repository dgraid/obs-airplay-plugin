#!/usr/bin/env bash
# Read-only audit of Zoom Workplace airhost.app.
# Does not attach, inject, re-sign, or copy Zoom binaries into the project.
set -euo pipefail

MODE="${1:-idle}"
ZOOM_APP="${ZOOM_APP:-/Applications/zoom.us.app}"
AIRHOST="${ZOOM_APP}/Contents/Frameworks/airhost.app"
BIN="${AIRHOST}/Contents/MacOS/airhost"
OUT_DIR="${2:-}"

ts() { date +"%Y-%m-%dT%H:%M:%S%z"; }

if [[ ! -x "$BIN" ]]; then
  echo "ERROR: airhost not found at $BIN" >&2
  exit 1
fi

echo "=== Zoom AirHost audit $(ts) mode=$MODE ==="
echo "ZOOM_APP=$ZOOM_APP"
echo

echo "== Zoom Info.plist =="
plutil -extract CFBundleIdentifier raw "${ZOOM_APP}/Contents/Info.plist" 2>/dev/null || true
plutil -extract CFBundleShortVersionString raw "${ZOOM_APP}/Contents/Info.plist" 2>/dev/null || true
plutil -extract CFBundleVersion raw "${ZOOM_APP}/Contents/Info.plist" 2>/dev/null || true
echo

echo "== airhost path / file / lipo =="
ls -la "$BIN"
file "$BIN"
lipo -info "$BIN" 2>/dev/null || true
echo

echo "== airhost Info.plist =="
plutil -p "${AIRHOST}/Contents/Info.plist" 2>/dev/null | head -60
echo

echo "== codesign =="
codesign -dv --verbose=4 "$AIRHOST" 2>&1 | head -40
echo

echo "== entitlements (xml) =="
codesign -d --entitlements - --xml "$AIRHOST" 2>/dev/null | head -120 || echo "(none)"
echo

echo "== otool -L =="
otool -L "$BIN"
echo

echo "== LC_RPATH =="
otool -l "$BIN" | awk '/cmd LC_RPATH/{p=1} p&&/path/{print; p=0}'
echo

echo "== nested dylibs/frameworks inside airhost.app =="
find "$AIRHOST" -name '*.dylib' -o -name '*.framework' | sort
echo

echo "== undefined symbols of interest =="
nm -u "$BIN" 2>/dev/null | egrep -i 'DNSService|IOSurface|xpc_|shm_|mmap|VTDecompress|AudioConverter|CVPixel|mach_msg' || true
echo

echo "== string markers (protocol/IPC names only) =="
strings "$BIN" | egrep -i '^_airplay\._tcp$|^_raop\._tcp$|BonjourHandler|AirPlayMirroring|IOSurface|FairPlay|MediaControlServer' | sort -u | head -40
echo

echo "== airhost process list =="
pgrep -lf '[a]irhost' || echo "(not running)"
echo

if [[ "$MODE" == "--live" || "$MODE" == "live" ]]; then
  PIDS=$(pgrep -x airhost || true)
  if [[ -z "$PIDS" ]]; then
    echo "== LIVE: airhost is not running =="
    echo "Open Zoom Workplace → meeting → Share → iPhone/iPad via AirPlay, leave the instructions visible, then re-run:"
    echo "  $0 --live"
    exit 2
  fi
  echo "== LIVE parent / children =="
  for pid in $PIDS; do
    ps -o pid,ppid,user,stat,etime,command -p "$pid"
    echo "-- lsof -nP -p $pid (LISTEN/UDP/unix) --"
    lsof -nP -p "$pid" 2>/dev/null | egrep 'LISTEN|UDP|unix|IPv4|IPv6' | head -80 || true
    echo "-- pstree-ish --"
    ps -o pid,ppid,command -p "$pid" -p "$(ps -o ppid= -p "$pid" | tr -d ' ')"
  done
  echo
  echo "== dns-sd browse (3s) =="
  if command -v dns-sd >/dev/null; then
    dns-sd -B _airplay._tcp local. &
    AP=$!
    dns-sd -B _raop._tcp local. &
    RA=$!
    sleep 3
    kill "$AP" "$RA" 2>/dev/null || true
    wait 2>/dev/null || true
  fi
  echo
  echo "== log show airhost last 2m (no packet payloads) =="
  log show --last 2m --style compact --predicate 'process == "airhost" OR (process == "zoom.us" AND eventMessage CONTAINS[c] "airhost")' 2>/dev/null | tail -40 || true
fi

echo "=== done $(ts) ==="
