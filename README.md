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
- CMake, C++17 compiler, SDL2, OpenGL, GLib/GIO, libsoup 3, GStreamer (core,
  app, video), the GStreamer PipeWire plugin, and OpenVR SDK headers and library

On Debian/Ubuntu, the non-VR build dependencies are typically:

```sh
sudo apt install build-essential cmake pkg-config libsdl2-dev libgl-dev \
  libglib2.0-dev libsoup-3.0-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-pipewire pipewire xdg-desktop-portal
```

On Arch Linux, install `libsoup3` alongside the existing build dependencies:

```sh
sudo pacman -S libsoup3
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

## Tracking WebSocket

While running, the app hosts `ws://127.0.0.1:8765/state`. Use `--ws-port PORT`
to choose another local port. Every VR frame produces one WebSocket text
message containing a complete JSON state. For example, a browser client can
read it with:

```js
const ws = new WebSocket("ws://127.0.0.1:8765/state");
ws.onmessage = event => console.log(JSON.parse(event.data));
```

The message has this shape:

```json
{
  "timestamp_ms": 1791300000000,
  "headset": {
    "connected": true,
    "pose": {
      "position_m": {"x": 0.0, "y": 1.7, "z": 0.0},
      "angles_deg": {"yaw": 0.0, "pitch": 0.0, "roll": 0.0}
    }
  },
  "controllers": {
    "left": {
      "connected": true,
      "device_index": 1,
      "pose": {
        "position_m": {"x": -0.2, "y": 1.3, "z": -0.3},
        "angles_deg": {"yaw": 0.0, "pitch": 0.0, "roll": 0.0}
      },
      "trigger": {"value": 0.5, "pressed": true},
      "grip": {"pressed": false, "touched": false}
    },
    "right": {
      "connected": false,
      "device_index": null,
      "pose": null,
      "trigger": {"value": null, "pressed": false},
      "grip": {"pressed": false, "touched": false}
    }
  }
}
```

Positions are metres in OpenVR tracking space (+X right, +Y up, -Z forward).
Angles use a Y-X-Z yaw, pitch, roll decomposition in degrees. `pose` is `null`
when tracking is invalid; trigger `value` is `null` if the legacy OpenVR API
does not expose a trigger axis. Trigger and grip buttons use OpenVR's legacy
controller state mapping, so their availability depends on the controller's
SteamVR bindings. The listener accepts connections from the local machine only.

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
