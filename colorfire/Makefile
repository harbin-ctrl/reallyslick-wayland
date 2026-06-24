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

.PHONY: all clean install uninstall

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

clean:
	rm -f $(TARGET) xdg-shell-client-protocol.h xdg-shell-protocol.c \
	       xdg-decoration-client-protocol.h xdg-decoration-protocol.c \
	       xdg-shell-protocol.o xdg-decoration-protocol.o colorfire_textures.o
