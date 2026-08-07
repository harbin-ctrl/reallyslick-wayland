XDG_SHELL_XML := /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
XDG_DECO_XML  := /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml

CXX      := g++
CC       := gcc
CXXFLAGS := -O2 -Wall $(shell pkg-config --cflags wayland-client wayland-egl egl gl)
CFLAGS   := -O2 $(shell pkg-config --cflags wayland-client)
LIBS     := $(shell pkg-config --libs wayland-client wayland-egl egl gl) -lGLU -lm

TARGET  := flux
HEADERS := xdg-shell-client-protocol.h xdg-decoration-client-protocol.h

PREFIX  := /usr/local
BINDIR  := $(PREFIX)/bin
APPDIR  := $(PREFIX)/share/applications
ICONDIR := $(PREFIX)/share/icons/hicolor

.PHONY: all clean install uninstall install-user uninstall-user

all: $(TARGET)

xdg-shell-protocol.o: xdg-shell-protocol.c xdg-shell-client-protocol.h
	$(CC) $(CFLAGS) -c -o $@ $<

xdg-decoration-protocol.o: xdg-decoration-protocol.c xdg-decoration-client-protocol.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): flux.cpp xdg-shell-protocol.o xdg-decoration-protocol.o $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ flux.cpp xdg-shell-protocol.o xdg-decoration-protocol.o $(LIBS)

xdg-shell-client-protocol.h: $(XDG_SHELL_XML)
	wayland-scanner client-header < $< > $@

xdg-shell-protocol.c: $(XDG_SHELL_XML)
	wayland-scanner private-code < $< > $@

xdg-decoration-client-protocol.h: $(XDG_DECO_XML)
	wayland-scanner client-header < $< > $@

xdg-decoration-protocol.c: $(XDG_DECO_XML)
	wayland-scanner private-code < $< > $@

# This target used to install lattice.desktop and lattice-icon-*.png, copied
# wholesale from lattice-wayland: installing flux put a menu entry named
# "Lattice" on the system, and on a machine that had never built lattice the
# install simply failed on the missing files. flux ships its own flux.desktop
# now.
#
# There is no icon loop because flux has no icons yet -- unlike its siblings,
# nobody has drawn them. flux.desktop still says Icon=flux, so the entry picks
# them up by name the day they land here; until then the desktop falls back to
# a generic icon. Installing another screensaver's artwork is not a stand-in.
install: $(TARGET)
	install -Dm755 $(TARGET)     $(DESTDIR)$(BINDIR)/$(TARGET)
	install -Dm644 flux.desktop  $(DESTDIR)$(APPDIR)/flux.desktop
	-update-desktop-database $(DESTDIR)$(APPDIR) 2>/dev/null || true

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(APPDIR)/flux.desktop

install-user: $(TARGET)
	$(MAKE) install PREFIX=$(HOME)/.local

uninstall-user:
	$(MAKE) uninstall PREFIX=$(HOME)/.local

clean:
	rm -f $(TARGET) xdg-shell-client-protocol.h xdg-shell-protocol.c \
	       xdg-decoration-client-protocol.h xdg-decoration-protocol.c \
	       xdg-shell-protocol.o xdg-decoration-protocol.o
