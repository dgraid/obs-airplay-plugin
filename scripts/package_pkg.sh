#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${1:-$ROOT/build/obs-airplay.plugin}"
IDENTIFIER="dev.local.obs-airplay"
VERSION="$(sed -n 's/^project(obs-airplay VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
VERSION="${VERSION:?missing project() VERSION in CMakeLists.txt}"

OUT_NAME="obs-airplay-${VERSION}-macos-arm64.pkg"
DIST_DIR="$ROOT/dist"
WORK="$DIST_DIR/pkgwork"
PKGROOT="$WORK/pkgroot"
SCRIPTS_DST="$WORK/scripts"
RES_DST="$WORK/resources"
COMPONENT="$WORK/obs-airplay-component.pkg"
PRODUCT="$DIST_DIR/$OUT_NAME"

if [[ "$(uname -m)" != "arm64" ]]; then
  echo "This build is arm64-only. uname -m=$(uname -m)" >&2
  exit 1
fi
if [[ ! -d "$SRC" ]]; then
  echo "Missing plugin bundle: $SRC" >&2
  echo "Build first (docs/BUILD.md), then re-run $0" >&2
  exit 1
fi

rm -rf "$WORK"
mkdir -p "$PKGROOT/Library/Application Support/obs-studio/plugins"
mkdir -p "$SCRIPTS_DST" "$RES_DST" "$DIST_DIR"

ditto "$SRC" "$PKGROOT/Library/Application Support/obs-studio/plugins/obs-airplay.plugin"
codesign --force --deep --sign - --timestamp=none \
  "$PKGROOT/Library/Application Support/obs-studio/plugins/obs-airplay.plugin" >/dev/null

cp "$ROOT/packaging/macos/scripts/preinstall" "$SCRIPTS_DST/preinstall"
cp "$ROOT/packaging/macos/scripts/postinstall" "$SCRIPTS_DST/postinstall"
chmod 0755 "$SCRIPTS_DST/preinstall" "$SCRIPTS_DST/postinstall"

cp "$ROOT/packaging/macos/resources/readme.txt" "$RES_DST/readme.txt"
{
  cat "$ROOT/packaging/macos/resources/notice.txt"
  cat "$ROOT/docs/LICENSE.UxPlay"
} > "$RES_DST/license.txt"

sed -e "s/@VERSION@/${VERSION}/g" -e "s/@COMPONENT@/$(basename "$COMPONENT")/g" \
  "$ROOT/packaging/macos/distribution.xml.in" > "$WORK/distribution.xml"

pkgbuild \
  --root "$PKGROOT" \
  --install-location / \
  --identifier "$IDENTIFIER" \
  --version "$VERSION" \
  --ownership recommended \
  --min-os-version 13.0 \
  --scripts "$SCRIPTS_DST" \
  "$COMPONENT"

productbuild \
  --distribution "$WORK/distribution.xml" \
  --resources "$RES_DST" \
  --package-path "$WORK" \
  "$PRODUCT"

rm -rf "$WORK"

echo "Built $PRODUCT"
echo "Give that file to the other Mac. Unsigned: Gatekeeper may require Open Anyway."
