XDG_SHELL_XML := /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
XDG_DECO_XML  := /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml

CXX      := g++
CC       := gcc
CXXFLAGS := -O2 -Wall $(shell pkg-config --cflags wayland-client wayland-egl egl gl)
CFLAGS   := -O2 $(shell pkg-config --cflags wayland-client)
LIBS     := $(shell pkg-config --libs wayland-client wayland-egl egl gl) -lGLU -lm

TARGET  := lattice
HEADERS := xdg-shell-client-protocol.h xdg-decoration-client-protocol.h

PREFIX  := /usr/local
BINDIR  := $(PREFIX)/bin
APPDIR  := $(PREFIX)/share/applications
ICONDIR := $(PREFIX)/share/icons/hicolor

.PHONY: all clean install uninstall

all: $(TARGET)

xdg-shell-protocol.o: xdg-shell-protocol.c xdg-shell-client-protocol.h
	$(CC) $(CFLAGS) -c -o $@ $<

xdg-decoration-protocol.o: xdg-decoration-protocol.c xdg-decoration-client-protocol.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): lattice.cpp xdg-shell-protocol.o xdg-decoration-protocol.o $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ lattice.cpp xdg-shell-protocol.o xdg-decoration-protocol.o $(LIBS)

xdg-shell-client-protocol.h: $(XDG_SHELL_XML)
	wayland-scanner client-header < $< > $@

xdg-shell-protocol.c: $(XDG_SHELL_XML)
	wayland-scanner private-code < $< > $@

xdg-decoration-client-protocol.h: $(XDG_DECO_XML)
	wayland-scanner client-header < $< > $@

xdg-decoration-protocol.c: $(XDG_DECO_XML)
	wayland-scanner private-code < $< > $@

install: $(TARGET)
	install -Dm755 $(TARGET)        $(DESTDIR)$(BINDIR)/$(TARGET)
	install -Dm644 lattice.desktop  $(DESTDIR)$(APPDIR)/lattice.desktop
	for sz in 16 32 48 64 128 256; do \
		install -Dm644 lattice-icon-$${sz}.png \
			$(DESTDIR)$(ICONDIR)/$${sz}x$${sz}/apps/lattice.png; \
	done
	-gtk-update-icon-cache -f -t $(DESTDIR)$(ICONDIR) 2>/dev/null || true
	-update-desktop-database $(DESTDIR)$(APPDIR) 2>/dev/null || true

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(APPDIR)/lattice.desktop
	for sz in 16 32 48 64 128 256; do \
		rm -f $(DESTDIR)$(ICONDIR)/$${sz}x$${sz}/apps/lattice.png; \
	done

clean:
	rm -f $(TARGET) xdg-shell-client-protocol.h xdg-shell-protocol.c \
	       xdg-decoration-client-protocol.h xdg-decoration-protocol.c \
	       xdg-shell-protocol.o xdg-decoration-protocol.o
