# Substrate-Wayland

A native Wayland/EGL port of the **Substrate** screensaver, originally created by Jared Tarbell and Mike Kershaw (2004). This port translates the original 2D screensaver hack to a modern standalone Wayland client utilizing a CPU backbuffer uploaded directly to a GPU texture for hardware-accelerated presentation via OpenGL.

No X11, no xscreensaver — pure native Wayland.

## Build requirements

### Packages (Debian / Ubuntu / Armbian)

```bash
sudo apt install \
    g++ \
    libwayland-dev \
    wayland-protocols \
    libegl-dev \
    libgl-dev
```

`wayland-protocols` supplies the XDG-shell and XDG-decoration XML files that the build generates C stubs from. `wayland-scanner` (used during the build) ships inside `libwayland-dev`.

### Arch / Manjaro

```bash
sudo pacman -S gcc wayland wayland-protocols mesa
```

### Fedora / RHEL

```bash
sudo dnf install gcc-c++ wayland-devel wayland-protocols-devel \
                 mesa-libEGL-devel mesa-libGL-devel
```

## Build

```bash
make
```

## Run

```bash
./substrate [options]
```

### Options

| Flag | Default | Description |
|------|---------|-------------|
| `--wireframe` / `--no-wireframe` | `--no-wireframe` | Draw only the structural lines, disabling sand-painting effects |
| `--seamless` / `--no-seamless` | `--no-seamless` | Enable wrapping at screen boundaries |
| `--max-cycles <N>` | `10000` | Restart and regenerate after N cycles (set to `0` to run indefinitely) |
| `--initial-cracks <N>` | `3` | Number of initial seed cracks |
| `--max-cracks <N>` | `100` | Maximum number of concurrent cracks allowed |
| `--sand-grains <N>` | `64` | Density of the sand painter grains |
| `--circle-percent <N>` | `33` | Percentage chance of starting circular cracks instead of straight lines |
| `--bg <color>` | `white` | Background color (accepts `white`, `black`, or hex format like `#201F21`) |
| `--fg <color>` | `black` | Crack line color (accepts `white`, `black`, or hex format) |
| `--fullscreen` | | Start in fullscreen mode |
| `--benchmark` | | Headless execution mode for performance telemetry |

### Keys

| Key | Action |
|-----|--------|
| `Space` or `←` or `→` | Restart/regenerate generation instantly |
| `F` or `F11` | Toggle fullscreen |
| `Q` or `Esc` | Quit |

## Install system-wide

```bash
sudo make install      # installs to /usr/local/bin, desktop entry, and hicolor icons
sudo make uninstall
```

## Implementation Notes

- **Texture Streaming:** Instead of relying on legacy X11 2D primitives (`XDrawPoint`, `XGC`), this port runs the generative algorithm entirely in a memory-mapped CPU buffer, uploading updates to the GPU at 60 FPS.
- **Modern OpenGL Pipeline:** The canvas is rendered onto a fullscreen quad using GLSL vertex and fragment shaders (GLSL 1.40).
- **Frame Pacing:** Uses Wayland native `wl_surface_frame` callbacks to achieve smooth, jitter-free presentation.
