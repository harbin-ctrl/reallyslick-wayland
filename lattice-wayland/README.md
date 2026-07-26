# lattice-wayland

A native Wayland port of the **Lattice** screensaver from
[Really Slick Screensavers](https://www.reallyslick.com/) by Terence M. Welsh
(GPL-2.0-or-later).  No X11, no xscreensaver — pure Wayland/EGL/OpenGL.

## Build requirements

### Packages (Debian / Ubuntu / Armbian)

```
sudo apt install \
    g++ \
    libwayland-dev \
    wayland-protocols \
    libglu1-mesa-dev \
    libegl-dev \
    libgl-dev
```

`wayland-protocols` supplies the XDG-shell and XDG-decoration XML files that
the build generates C stubs from.  `wayland-scanner` (used during the build)
ships inside `libwayland-dev` on Debian/Ubuntu.

### Arch / Manjaro

```
sudo pacman -S gcc wayland wayland-protocols glu mesa
```

### Fedora / RHEL

```
sudo dnf install gcc-c++ wayland-devel wayland-protocols-devel \
                 mesa-libGLU-devel mesa-libEGL-devel mesa-libGL-devel
```

## Build

```
make
```

## Run

```
./lattice [--preset NAME] [options]
```

### Presets

| Name | Description |
|------|-------------|
| `brassmesh` | Square-profile rings, brass material *(default)* |
| `regular` | Default look, random colours |
| `chainmail` | Dense smooth rings, silver material |
| `computer` | Chunky dense rings, random colours |
| `slick` | Large thick smooth rings, pearl material |
| `tasty` | Same as slick, slightly sparser |

Cycle presets at runtime with the **← →** arrow keys.

### Options

| Flag | Range | Description |
|------|-------|-------------|
| `--preset NAME` | | Apply a named preset |
| `--speed N` | 1–100 | Flight and animation speed |
| `--depth N` | 1–10 | Draw depth (lattice cells ahead/behind) |
| `--density N` | 1–100 | Ring density per cell |
| `--thick N` | 1–100 | Ring tube thickness |
| `--fov N` | 10–150 | Horizontal field of view in degrees |
| `--longitude N` | 4–100 | Torus longitude segments |
| `--latitude N` | 2–100 | Torus latitude segments |
| `--pathrand N` | 1–10 | Path randomness |
| `--smooth` / `--no-smooth` | | Smooth vs flat shading |
| `--fog` / `--no-fog` | | Distance fog |

### Keys

| Key | Action |
|-----|--------|
| `← →` | Cycle presets |
| `F` or `F11` | Toggle fullscreen |
| `Q` or `Esc` | Quit |

## Install system-wide

```
sudo make install      # installs to /usr/local/bin, .desktop entry, icons
sudo make uninstall
```

## Notes

- Requires a running Wayland compositor (KWin, Sway, Mutter, etc.).
- Frame pacing uses `wl_surface_frame` callbacks.  Physics run at a fixed
  60 fps equivalent regardless of display refresh rate, matching the feel of
  the original screensaver on period hardware.
- The projection matrix uses the horizontal FOV convention of the original
  lattice screensaver (unlike `gluPerspective`, which is vertical): `--fov`
  sets the horizontal angle and the vertical angle narrows automatically
  with the screen's aspect ratio.
