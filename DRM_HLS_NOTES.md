# DRM live playback: dynamic HLS

The DRM downloader still writes decrypted fMP4 initialization segments and `.m4s` fragments under `~/kodigui/drm`.

The former periodic FFmpeg remux into a standalone `drm.mp4` file has been replaced by a lightweight playlist publisher:

- `~/kodigui/drm/hls/master.m3u8` — master playlist opened by Kodi;
- `~/kodigui/drm/hls/video.m3u8` — H.264 fMP4 video fragments;
- `~/kodigui/drm/hls/audio.m3u8` — AAC fMP4 audio fragments.

The publisher matches video and audio fragments by their filename timestamps (video time base 1/600, audio time base 1/48000), waits until the newest pair is stable, and atomically replaces playlist files. It does not rewrite or concatenate media fragments and does not run FFmpeg.

Both DRM Play buttons open `kodigui/drm/hls/master.m3u8` once. The EOF monitor and automatic `Player.Open` / `Player.Seek` resume logic have been removed.

Logs on Raspberry Pi:

- `~/kodigui/drm-download.log`
- `~/kodigui/drm-mux.log`

The HLS playlist generator was syntax-checked and tested with synthetic segment names matching the observed timestamp pattern. A Qt build and an end-to-end Kodi playback test still need to be run on the target build machine/device; gapless playback depends on Kodi's HLS demuxer and the actual segment keyframe/timestamp structure.
