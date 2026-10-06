# Window to SteamVR

Streams a Wayland window into SteamVR as side-by-side stereo: the captured
window's left half goes to the left eye, and its right half goes to the right
eye. The desktop's ScreenCast portal presents the window picker. No screen
capture permission bypass or X11 fallback is used.

The source window must already contain a side-by-side stereo image. Splitting
an ordinary desktop window will give each eye a different half of that window,
not a 3D image. The app captures live video; it does not save a recording.

## Requirements

- Linux Wayland session with a working `xdg-desktop-portal` ScreenCast backend
  and PipeWire
- SteamVR running with an OpenVR-compatible headset
- CMake, C++17 compiler, SDL2, OpenGL, GLib/GIO, GStreamer (core, app, video),
  the GStreamer PipeWire plugin, and OpenVR SDK headers and library

On Debian/Ubuntu, the non-VR build dependencies are typically:

```sh
sudo apt install build-essential cmake pkg-config libsdl2-dev libgl-dev \
  libglib2.0-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-pipewire pipewire xdg-desktop-portal
```

Install the matching desktop portal backend as well (for example,
`xdg-desktop-portal-gnome` or `xdg-desktop-portal-kde`). For OpenVR, install
`libopenvr-dev` if your distribution provides it, or download the
[OpenVR SDK](https://github.com/ValveSoftware/openvr) and set `OPENVR_ROOT` to
the SDK directory containing `headers/` and `lib/linux64/`.

## Build and run

```sh
cmake -S . -B build -DOPENVR_ROOT=/path/to/openvr
cmake --build build -j
./build/window-to-steamvr
```

Choose the stereo window in the portal dialog. The app copies each half of the
captured frame to a separate OpenGL texture and submits one texture per eye to
SteamVR. Press Ctrl+C in the terminal to stop. Close the selected window to end
the stream.

This program submits the image directly to the compositor, so it fills each
eye's view. It does not place the image on a virtual theater screen or apply
stereo depth adjustments.

## If the window picker crashes

If a crash dialog names `xdg-desktop-portal`, the desktop's screen-sharing
service exited. The app prints the portal step it reached before the crash.
Collect that terminal output and the service log:

```sh
journalctl --user -b -u xdg-desktop-portal -n 100 --no-pager
coredumpctl info xdg-desktop-portal
```

Also note your distribution, desktop environment, and versions of
`xdg-desktop-portal` and its desktop-specific backend. The portal and backend
must support window ScreenCast sources; a compositor or portal failure needs
to be fixed in those components.
