OBS AirPlay Receiver (arm64)

Installs into (no admin password):
  ~/Library/Application Support/obs-studio/plugins/obs-airplay.plugin

OBS may stay open. The installer finishes anyway and offers a restart so OBS loads the new plugin. Restart is optional. Does not write into /Applications/OBS.app.

This package is ad-hoc signed and not notarized (no Apple Developer ID).

If macOS says the developer cannot be verified:
  1. Control-click the .pkg → Open → Open
  2. Or System Settings → Privacy & Security → Open Anyway

AirDrop / USB often skips quarantine; a download from the internet usually does not.

macOS 26: the installer rewrites the installed plugin onto a new inode so OBS can load it after a GitHub download. Restart OBS after install. Homebrew is not required.

После установки: OBS → Sources → + → AirPlay Receiver.
Если macOS пишет «разработчик не проверен»: Control-click по .pkg → Открыть.
Если OBS пишет, что плагин не загрузился — поставь эту версию ещё раз и перезапусти OBS.
