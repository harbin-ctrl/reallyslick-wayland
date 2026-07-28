# Wayland Slick Toys

Eight screensavers from [Really Slick Screensavers](https://github.com/sirspudd/rss-glx), ported to run
natively on Wayland as desktop toys rather than screensavers. Written for a Raspberry Pi 400, but nothing in
them is Pi-specific.

The originals were GLX programs that took over the screen. These render to an ordinary Wayland surface through
EGL, so they can sit in a window, fill the desktop, or run behind whatever else you are doing.

| | from | notes |
|---|---|---|
| `colorfire` | Really Slick | |
| `flurry-wayland` | Flurry | |
| `flux-wayland` | Flux | `flux_orig.cpp` is kept beside the port for comparison |
| `lattice-wayland` | Lattice | |
| `pixelcity-wayland` | Pixel City (Shamus Young) | the largest of them |
| `skyrocket-wayland` | Skyrocket | |
| `solarwinds-wayland` | Solar Winds | |
| `substrate` | Substrate | |

## Building

Each lives in its own directory with its own Makefile:

    cd lattice-wayland && make

They share a dependency on `wayland-client`, `wayland-egl`, `EGL` and `GL`, and each generates its own
`xdg-shell` and `xdg-decoration` protocol sources with `wayland-scanner` at build time. Those generated files
are not in the repository.

## Why one repository

These began as eight separate repositories, which was eight sets of clone URLs, eight licence files and eight
places to look for a shared fix — for eight programs that are the same thing eight times. Really Slick
Screensavers ships as one collection; so does this. Each directory's history was preserved when they were
brought together, so `git log -- lattice-wayland/` still shows that screensaver's development on its own.

## Licence

GPL-2.0, matching Really Slick Screensavers, from which all of these derive. See `LICENSE`.

Pixel City is derived from Shamus Young's work of the same name; Flurry, Flux, Lattice, Solar Winds,
Skyrocket, Colorfire and Substrate from the rss-glx collection. The Wayland and EGL ports are mine, offered
under the same terms.
