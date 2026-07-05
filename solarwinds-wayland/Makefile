XDG_SHELL_XML := /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
XDG_DECO_XML  := /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml

CXX      := g++
CC       := gcc
CXXFLAGS := -O3 -Wall $(shell pkg-config --cflags wayland-client wayland-egl egl gl)
CFLAGS   := -O3 $(shell pkg-config --cflags wayland-client)
LIBS     := $(shell pkg-config --libs wayland-client wayland-egl egl gl) -lGLU -lm

TARGET  := solarwinds
HEADERS := xdg-shell-client-protocol.h xdg-decoration-client-protocol.h
OBJS    := main.o solarwinds.o xdg-shell-protocol.o xdg-decoration-protocol.o

PREFIX  ?= /usr/local
BINDIR  := $(PREFIX)/bin
APPDIR  := $(PREFIX)/share/applications
ICONDIR := $(PREFIX)/share/icons/hicolor

.PHONY: all clean install uninstall install-user uninstall-user

all: $(TARGET)

xdg-shell-protocol.o: xdg-shell-protocol.c xdg-shell-client-protocol.h
	$(CC) $(CFLAGS) -c -o $@ $<

xdg-decoration-protocol.o: xdg-decoration-protocol.c xdg-decoration-client-protocol.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

xdg-shell-client-protocol.h: $(XDG_SHELL_XML)
	wayland-scanner client-header < $< > $@

xdg-shell-protocol.c: $(XDG_SHELL_XML)
	wayland-scanner private-code < $< > $@

xdg-decoration-client-protocol.h: $(XDG_DECO_XML)
	wayland-scanner client-header < $< > $@

xdg-decoration-protocol.c: $(XDG_DECO_XML)
	wayland-scanner private-code < $< > $@

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -Dm644 solarwinds.desktop $(DESTDIR)$(APPDIR)/solarwinds.desktop
	for sz in 16 32 48 64 128 256 512; do \
		install -Dm644 icons/solarwinds-icon-$${sz}.png \
			$(DESTDIR)$(ICONDIR)/$${sz}x$${sz}/apps/solarwinds.png; \
	done
	-gtk-update-icon-cache -f -t $(DESTDIR)$(ICONDIR) 2>/dev/null || true
	-update-desktop-database $(DESTDIR)$(APPDIR) 2>/dev/null || true

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(APPDIR)/solarwinds.desktop
	for sz in 16 32 48 64 128 256 512; do \
		rm -f $(DESTDIR)$(ICONDIR)/$${sz}x$${sz}/apps/solarwinds.png; \
	done

install-user: $(TARGET)
	install -d "$(HOME)/.local/bin"
	install -m 755 $(TARGET) "$(HOME)/.local/bin/$(TARGET)"
	install -d "$(HOME)/.local/share/applications"
	install -m 644 solarwinds.desktop "$(HOME)/.local/share/applications/solarwinds.desktop"
	for sz in 16 32 48 64 128 256 512; do \
		install -d "$(HOME)/.local/share/icons/hicolor/$${sz}x$${sz}/apps"; \
		install -m 644 icons/solarwinds-icon-$${sz}.png "$(HOME)/.local/share/icons/hicolor/$${sz}x$${sz}/apps/solarwinds.png"; \
	done
	-gtk-update-icon-cache -f -t "$(HOME)/.local/share/icons/hicolor" 2>/dev/null || true
	-update-desktop-database "$(HOME)/.local/share/applications" 2>/dev/null || true

uninstall-user:
	rm -f "$(HOME)/.local/bin/$(TARGET)"
	rm -f "$(HOME)/.local/share/applications/solarwinds.desktop"
	for sz in 16 32 48 64 128 256 512; do \
		rm -f "$(HOME)/.local/share/icons/hicolor/$${sz}x$${sz}/apps/solarwinds.png"; \
	done

clean:
	rm -f $(TARGET) *.o xdg-shell-protocol.c xdg-shell-client-protocol.h xdg-decoration-protocol.c xdg-decoration-client-protocol.h
