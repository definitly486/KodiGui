# OpenWV support for the Okko tab

KodiGui can pass an external Widevine CDM path to Qt WebEngine. The application
does not contain OpenWV, a Widevine device identity, or license-server credentials.

## Requirements

- FreeBSD amd64 with the existing Qt 6 WebEngine installation.
- Git, Rust/Cargo, and Clang++.
- A Widevine device identity (`.wvd`) that you are authorized to use.
- A content-provider account and playback rights for the protected content.

OpenWV does not include a device identity. It embeds the supplied `.wvd`
during compilation, so the resulting `libwidevinecdm.so` contains sensitive
device credentials. Never commit the `.wvd` file or the resulting library to
GitHub, and do not share them.

## Build and install OpenWV

From the KodiGui repository root:

```sh
chmod +x scripts/build-openwv.sh
./scripts/build-openwv.sh /absolute/path/to/your-authorized-device.wvd
```

The script clones OpenWV and its submodules into a temporary directory, builds
the release library, and installs it at:

```
~/.local/share/KodiGui/widevine/libwidevinecdm.so
```

The temporary source tree (including the copied `.wvd`) is removed when the
script exits. The installed CDM library remains local to your account.

To use a different library path, set `KODIGUI_WIDEVINE_PATH` before launching
KodiGui:

```sh
KODIGUI_WIDEVINE_PATH=/path/to/libwidevinecdm.so ./kodigui
```

KodiGui appends Qt WebEngine's `--widevine-path` option to its existing
Chromium flags. Restart the application after installing or changing the CDM.

## Limitations

- This configures the CDM path; it does not guarantee that a particular
  QtWebEngine/Chromium build accepts the CDM ABI or that Okko grants a license.
- The content provider still controls authentication, device authorization,
  output protection, resolution, and license policy.
- A software CDM/device identity does not provide hardware-backed Widevine L1.
- If Qt WebEngine reports that Widevine is unsupported or fails to load the
  library, collect the `[Widevine]` and Chromium startup logs before changing
  the system Qt packages.

OpenWV is licensed under LGPL-3.0. See
https://github.com/tchebb/openwv for its source and full build documentation.
