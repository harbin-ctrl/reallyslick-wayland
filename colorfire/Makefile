XDG_SHELL_XML := /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
XDG_DECO_XML  := /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml

CXX      := g++
CC       := gcc
CXXFLAGS := -O2 -Wall $(shell pkg-config --cflags wayland-client wayland-egl egl gl)
CFLAGS   := -O2 $(shell pkg-config --cflags wayland-client)
LIBS     := $(shell pkg-config --libs wayland-client wayland-egl egl gl) -lGLU -lm

TARGET  := colorfire
HEADERS := xdg-shell-client-protocol.h xdg-decoration-client-protocol.h

PREFIX  := /usr/local
BINDIR  := $(PREFIX)/bin
APPDIR  := $(PREFIX)/share/applications
ICONDIR := $(PREFIX)/share/icons/hicolor
DT_ICON_SIZES := 16 22 24 32 48 64 128 256 512

.PHONY: all clean install uninstall install-user uninstall-user

all: $(TARGET)

xdg-shell-protocol.o: xdg-shell-protocol.c xdg-shell-client-protocol.h
	$(CC) $(CFLAGS) -c -o $@ $<

xdg-decoration-protocol.o: xdg-decoration-protocol.c xdg-decoration-client-protocol.h
	$(CC) $(CFLAGS) -c -o $@ $<

colorfire_textures.o: colorfire_textures.cpp colorfire_textures.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(TARGET): colorfire.cpp colorfire_textures.o xdg-shell-protocol.o xdg-decoration-protocol.o $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ colorfire.cpp colorfire_textures.o xdg-shell-protocol.o xdg-decoration-protocol.o $(LIBS)

xdg-shell-client-protocol.h: $(XDG_SHELL_XML)
	wayland-scanner client-header < $< > $@

xdg-shell-protocol.c: $(XDG_SHELL_XML)
	wayland-scanner private-code < $< > $@

xdg-decoration-client-protocol.h: $(XDG_DECO_XML)
	wayland-scanner client-header < $< > $@

xdg-decoration-protocol.c: $(XDG_DECO_XML)
	wayland-scanner private-code < $< > $@

install: $(TARGET)
	install -Dm755 $(TARGET)          $(DESTDIR)$(BINDIR)/$(TARGET)
	install -Dm644 colorfire.desktop  $(DESTDIR)$(APPDIR)/colorfire.desktop
	install -d "$(DESTDIR)$(PREFIX)/share/desktop-directories"
	install -m 644 DesktopToys.directory "$(DESTDIR)$(PREFIX)/share/desktop-directories/DesktopToys.directory"
	install -d "$(DESTDIR)/etc/xdg/menus/applications-merged"
	install -m 644 desktop-toys.menu "$(DESTDIR)/etc/xdg/menus/applications-merged/desktop-toys.menu"
	install -d "$(DESTDIR)/etc/xdg/menus/rpd-applications-merged"
	install -m 644 desktop-toys.menu "$(DESTDIR)/etc/xdg/menus/rpd-applications-merged/desktop-toys.menu"
	for sz in 16 32 48 64 128 256 512; do \
		install -Dm644 colorfire-icon-$${sz}.png \
			$(DESTDIR)$(ICONDIR)/$${sz}x$${sz}/apps/colorfire.png; \
	done
	for sz in $(DT_ICON_SIZES); do \
		install -d "$(DESTDIR)$(ICONDIR)/$${sz}x$${sz}/apps"; \
		install -m 644 desktop-toys-icon-$${sz}.png "$(DESTDIR)$(ICONDIR)/$${sz}x$${sz}/apps/desktop-toys.png"; \
	done
	-gtk-update-icon-cache -f -t $(DESTDIR)$(ICONDIR) 2>/dev/null || true
	-update-desktop-database $(DESTDIR)$(APPDIR) 2>/dev/null || true

install-user: $(TARGET)
	install -d "$(HOME)/.local/bin"
	install -m 755 $(TARGET) "$(HOME)/.local/bin/$(TARGET)"
	install -d "$(HOME)/.local/share/applications"
	install -m 644 colorfire.desktop "$(HOME)/.local/share/applications/colorfire.desktop"
	install -d "$(HOME)/.local/share/desktop-directories"
	install -m 644 DesktopToys.directory "$(HOME)/.local/share/desktop-directories/DesktopToys.directory"
	install -d "$(HOME)/.config/menus/applications-merged"
	install -m 644 desktop-toys.menu "$(HOME)/.config/menus/applications-merged/desktop-toys.menu"
	install -d "$(HOME)/.config/menus/rpd-applications-merged"
	install -m 644 desktop-toys.menu "$(HOME)/.config/menus/rpd-applications-merged/desktop-toys.menu"
	for sz in 16 32 48 64 128 256 512; do \
		install -d "$(HOME)/.local/share/icons/hicolor/$${sz}x$${sz}/apps"; \
		install -m 644 colorfire-icon-$${sz}.png "$(HOME)/.local/share/icons/hicolor/$${sz}x$${sz}/apps/colorfire.png"; \
	done
	for sz in $(DT_ICON_SIZES); do \
		install -d "$(HOME)/.local/share/icons/hicolor/$${sz}x$${sz}/apps"; \
		install -m 644 desktop-toys-icon-$${sz}.png "$(HOME)/.local/share/icons/hicolor/$${sz}x$${sz}/apps/desktop-toys.png"; \
	done
	-gtk-update-icon-cache -f -t "$(HOME)/.local/share/icons/hicolor" 2>/dev/null || true
	-update-desktop-database "$(HOME)/.local/share/applications" 2>/dev/null || true

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(APPDIR)/colorfire.desktop
	for sz in 16 32 48 64 128 256 512; do \
		rm -f $(DESTDIR)$(ICONDIR)/$${sz}x$${sz}/apps/colorfire.png; \
	done
	-gtk-update-icon-cache -f -t $(DESTDIR)$(ICONDIR) 2>/dev/null || true
	-update-desktop-database $(DESTDIR)$(APPDIR) 2>/dev/null || true

uninstall-user:
	rm -f "$(HOME)/.local/bin/$(TARGET)"
	rm -f "$(HOME)/.local/share/applications/colorfire.desktop"
	for sz in 16 32 48 64 128 256 512; do \
		rm -f "$(HOME)/.local/share/icons/hicolor/$${sz}x$${sz}/apps/colorfire.png"; \
	done
	-gtk-update-icon-cache -f -t "$(HOME)/.local/share/icons/hicolor" 2>/dev/null || true
	-update-desktop-database "$(HOME)/.local/share/applications" 2>/dev/null || true

clean:
	rm -f $(TARGET) xdg-shell-client-protocol.h xdg-shell-protocol.c \
	       xdg-decoration-client-protocol.h xdg-decoration-protocol.c \
	       xdg-shell-protocol.o xdg-decoration-protocol.o colorfire_textures.o

