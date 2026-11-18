# Okko browser extensions

The Okko tab has a small extension manager backed by Qt WebEngine's
`QWebEngineExtensionManager` (introduced in Qt 6.10).

## Install an extension

1. Build and start KodiGui.
2. Open the `okko.tv` tab.
3. Click **Установить расширение**.
4. Select the unpacked extension directory. Its `manifest.json` must be in
   the selected directory's top level.
5. The extension is installed into the persistent Okko WebEngine profile and
   enabled automatically. It is loaded again on later application starts.

Use **Расширения** to enable or disable loaded extensions. Disabled states are
saved in KodiGui settings.

## Compatibility

Qt WebEngine 6.11 accepts **Manifest V3** Chrome-compatible extensions only.
Manifest V2 extensions and Firefox-only APIs are not supported. Some Firefox
WebExtensions that use only APIs also implemented by Chromium may work, but
compatibility is not guaranteed. An XPI archive is not a Firefox runtime and
does not make Gecko APIs available.

The Okko tab now uses its own persistent WebEngine profile for cookies,
local storage, cache, and installed extensions. You may need to sign in to
Okko again once after the profile change.

## FreeBSD build

The project already links `webenginecore` and `webenginewidgets`. The
extension manager API is available starting with Qt 6.10, so build with the
Qt WebEngine 6.11 packages used by this project.
