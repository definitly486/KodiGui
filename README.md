# KodiGui

Qt 6 desktop application for controlling Kodi and related streaming workflows.

## Linux build

Supported CI target: Ubuntu 24.04 x86_64. The project uses Qt 6 Widgets, Qt WebEngine, Qt Network, Qt Concurrent and libssh2.

Install build dependencies:

```bash
sudo bash ./scripts/install-linux-deps.sh
```

Or install them manually:

```bash
sudo apt-get update
sudo apt-get install -y build-essential pkg-config qt6-base-dev qt6-webengine-dev qt6-tools-dev-tools libssh2-1-dev
```

Build:

```bash
bash ./scripts/build-linux.sh
```

The executable is created as `./kodigui`.

## Okko and Widevine DRM

The **okko.tv** tab opens Okko inside the application's embedded Qt WebEngine browser. It is not an external browser. The tab has its own persistent browser profile, so the Okko login/session is kept between application launches on the computer running KodiGui.

For DRM-protected playback, Qt WebEngine must be able to load a compatible Widevine Content Decryption Module (CDM). KodiGui does not include a CDM or device credentials in the repository.

### 1. Prepare the Widevine CDM

The project provides `scripts/build-openwv.sh` to build OpenWV from a locally supplied, authorized `.wvd` device file. The file contains sensitive device identity material. Use only credentials and device files that you are authorized to use; do not upload them to GitHub or share them.

Install the build tools on Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y git cargo rustc clang++
```

Build and install the CDM (replace the example path with the actual path to your authorized file):

```bash
bash ./scripts/build-openwv.sh "$HOME/path/to/authorized-device.wvd"
```

On success, the script installs:

```
~/.local/share/KodiGui/widevine/libwidevinecdm.so
```

The script clones OpenWV into a temporary directory, embeds the supplied device identity while building, copies the resulting library to the local KodiGui data directory, and removes the temporary build directory. The `.wvd` file is not copied into the KodiGui repository.

Keep both the original `.wvd` file and the generated `libwidevinecdm.so` private. Do not commit either file, add them to release archives, or send them in bug reports.

### 2. Start KodiGui with the CDM

KodiGui checks for the CDM at the default path above and passes it to Qt WebEngine using `--widevine-path`.

If you previously set `KODIGUI_WIDEVINE_PATH` to an old or invalid location, it overrides the default. To use the standard installation, start the application with that variable unset:

```bash
cd /path/to/KodiGui
env -u KODIGUI_WIDEVINE_PATH ./kodigui
```

Alternatively, set the variable to the full path of a valid CDM:

```bash
KODIGUI_WIDEVINE_PATH="$HOME/.local/share/KodiGui/widevine/libwidevinecdm.so" ./kodigui
```

Restart KodiGui after installing or replacing the CDM.

### 3. Sign in and test playback

1. Start KodiGui and open the **okko.tv** tab.
2. Sign in to your Okko account in the embedded page.
3. Open a title that is available to your account and start playback.
4. If the provider permits playback in this browser environment and the CDM is compatible, the video should start in the embedded player.
5. The **Диагностика OpenWV** panel at the bottom of the tab displays CDM diagnostic messages. The MPD field above the page displays a DASH manifest URL when the page requests one; use **Копировать MPD** to copy that URL for diagnostics.

The Okko browser profile is stored locally under the application's `AppDataLocation/okko-profile` directory. It contains browser data and persistent cookies. Do not delete it if you want to keep the current Okko session. To sign out, use Okko's own account controls in the page.

### 4. Troubleshooting

- **`[Widevine] CDM not found`** — check that `libwidevinecdm.so` exists at the default path, or set `KODIGUI_WIDEVINE_PATH` to its actual location.
- **The wrong CDM path is being used** — unset a stale `KODIGUI_WIDEVINE_PATH` as shown above.
- **CDM loads but video does not play** — check the OpenWV diagnostic panel, confirm that the CDM is compatible with the Qt WebEngine/Chromium version and platform, and verify that the title is playable on the account. A detected CDM does not guarantee that the service will authorize playback.
- **Okko asks you to sign in again** — the profile is local to the machine running KodiGui. Check that the application can write to its `AppDataLocation/okko-profile` directory and that browser cookies have not been cleared.
- **The page opens but playback is unavailable** — Qt WebEngine, the CDM, the service's browser/device policy, account entitlements and regional availability must all permit playback. KodiGui cannot override restrictions imposed by the service.
- **TLS/certificate errors** — KodiGui logs certificate errors and rejects untrusted certificates. Do not disable certificate validation to work around them.

Do not post diagnostic logs, screenshots, MPD URLs, account cookies, device files or CDM binaries publicly. Treat session data and DRM-related diagnostic output as sensitive.

## Runtime notes

- Run from a graphical Linux desktop session (X11 or XWayland).
- Qt WebEngine is required for the embedded Okko browser and other embedded web playback.
- Python 3 is detected from `PATH` when the Match TV helper is started.
- Remote Kodi operations use the in-process `libssh2` SSH client. A system `ssh`, PuTTY or `sshpass` executable is not required. Configure the host, port, username, password or private key in **Настройки → SSH**.
- The URL-page **Info** button runs the standalone Streamlink bundled in `runtime/` in the Linux release archive, on the computer running KodiGui; it does not require Python or a system Streamlink installation. Streamlink actions on the YouTube and Match TV pages remain separate and continue to run on the configured Kodi host. Other helpers such as `yt-dlp`, `ffmpeg` and `N_m3u8DL-RE` must be installed on the machine where their commands are executed.
- The URL tab's **kodidlp → run** action uses the same bundled local Streamlink to select the third available quality (or the last available quality if fewer than three are listed), creates a temporary `list.m3u`, uploads it to `/var/www/html/list.m3u` over the SSH/SFTP connection configured in **Настройки → SSH**, stops Kodi playback, performs the two `reloadlist` enable/disable toggles, waits, and opens PVR channel ID `1`. The `sshpass` utility and the two standalone shell scripts are not required.
- The application connects to the configured Kodi/Raspberry Pi host over SSH and Kodi JSON-RPC. Update the host/user settings in the source before using it on a different network.
- Widevine playback requires a separately supplied compatible CDM. Set `KODIGUI_WIDEVINE_PATH` to its `libwidevinecdm.so` path, or place it at `~/.local/share/KodiGui/widevine/libwidevinecdm.so`. No Widevine binary is bundled.

## GitHub Actions

The Linux workflow builds on pushes and pull requests targeting `main` or `feature/linux-support`, on `v*` tags, and on manual dispatch. It uploads the `kodigui-linux-x86_64` archive and checksum as a workflow artifact. Pushing a tag such as `v1.0.0` also creates or updates a GitHub Release and attaches the archive and checksum.

The CI artifact is a dynamically linked Linux executable; Qt 6, Qt WebEngine and libssh2 runtime libraries must be installed on the target system. The Widevine CDM is not part of the CI artifact and must be installed separately on each machine where DRM playback is required.
