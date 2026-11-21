# KodiGui

Qt 6 desktop application for controlling Kodi and related streaming workflows.

## Linux build

Supported CI target: Ubuntu 24.04 x86_64. The project uses Qt 6 Widgets, Qt WebEngine, Qt Network, Qt Concurrent and libssh2.

Install build dependencies:

```bash
sudo ./scripts/install-linux-deps.sh
```

Or install them manually:

```bash
sudo apt-get update
sudo apt-get install -y build-essential pkg-config qt6-base-dev qt6-webengine-dev qt6-tools-dev-tools libssh2-1-dev
```

Build:

```bash
./scripts/build-linux.sh
```

The executable is created as `./kodigui`.

## Runtime notes

- Run from a graphical Linux desktop session (X11 or XWayland).
- Qt WebEngine is required for embedded web playback.
- Python 3 is detected from `PATH` when the Match TV helper is started.
- Remote Kodi operations use OpenSSH. Install `openssh-client`; password-based SSH actions may also require `sshpass`.
- Streaming helpers such as `yt-dlp`, `streamlink`, `ffmpeg` and `N_m3u8DL-RE` must be installed on the machine where the corresponding commands are executed.
- The application connects to the configured Kodi/Raspberry Pi host over SSH and Kodi JSON-RPC. Update the host/user settings in the source before using it on a different network.
- Widevine playback requires a separately supplied compatible CDM. Set `KODIGUI_WIDEVINE_PATH` to its `libwidevinecdm.so` path, or place it at `~/.local/share/KodiGui/widevine/libwidevinecdm.so`. No Widevine binary is bundled.

## GitHub Actions

The Linux workflow builds on pushes and pull requests targeting `main`, on `v*` tags, and on manual dispatch. It uploads the `kodigui-linux-x86_64` archive and checksum as a workflow artifact. Tag builds are also attached to the corresponding GitHub Release.

The CI artifact is a dynamically linked Linux executable; Qt 6, Qt WebEngine and libssh2 runtime libraries must be installed on the target system.
