TARGET  := skyrocket
SRCS    := main.cpp skyrocket.cpp skyrocket_flare.cpp skyrocket_particle.cpp \
           skyrocket_shockwave.cpp skyrocket_smoke.cpp skyrocket_sound.cpp \
           skyrocket_textures.cpp skyrocket_world.cpp rsMath.c \
           rsMath/rsMatrix.cpp rsMath/rsQuat.cpp rsMath/rsTrigonometry.c rsMath/rsVec.cpp
OBJS    := $(patsubst %.cpp,%.o,$(patsubst %.c,%.o,$(SRCS)))

CXXFLAGS := -O3 -Wall -I. -I./rsMath $(shell pkg-config --cflags sdl2 gl)
CFLAGS   := -O3 -Wall -I. -I./rsMath $(shell pkg-config --cflags sdl2)
LIBS     := $(shell pkg-config --libs sdl2 gl) -lGLU -lm

PREFIX  ?= /usr/local
BINDIR  := $(PREFIX)/bin
APPDIR  := $(PREFIX)/share/applications
ICONDIR := $(PREFIX)/share/icons/hicolor

.PHONY: all clean install uninstall install-user uninstall-user

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -Dm644 skyrocket.desktop $(DESTDIR)$(APPDIR)/skyrocket.desktop
	for sz in 16 32 48 64 128 256 512; do \
		install -Dm644 icons/skyrocket-icon-$${sz}.png \
			$(DESTDIR)$(ICONDIR)/$${sz}x$${sz}/apps/skyrocket.png; \
	done
	-gtk-update-icon-cache -f -t $(DESTDIR)$(ICONDIR) 2>/dev/null || true
	-update-desktop-database $(DESTDIR)$(APPDIR) 2>/dev/null || true

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(APPDIR)/skyrocket.desktop
	for sz in 16 32 48 64 128 256 512; do \
		rm -f $(DESTDIR)$(ICONDIR)/$${sz}x$${sz}/apps/skyrocket.png; \
	done

install-user: $(TARGET)
	install -d "$(HOME)/.local/bin"
	install -m 755 $(TARGET) "$(HOME)/.local/bin/$(TARGET)"
	install -d "$(HOME)/.local/share/applications"
	install -m 644 skyrocket.desktop "$(HOME)/.local/share/applications/skyrocket.desktop"
	for sz in 16 32 48 64 128 256 512; do \
		install -d "$(HOME)/.local/share/icons/hicolor/$${sz}x$${sz}/apps"; \
		install -m 644 icons/skyrocket-icon-$${sz}.png "$(HOME)/.local/share/icons/hicolor/$${sz}x$${sz}/apps/skyrocket.png"; \
	done
	-gtk-update-icon-cache -f -t "$(HOME)/.local/share/icons/hicolor" 2>/dev/null || true
	-update-desktop-database "$(HOME)/.local/share/applications" 2>/dev/null || true

uninstall-user:
	rm -f "$(HOME)/.local/bin/$(TARGET)"
	rm -f "$(HOME)/.local/share/applications/skyrocket.desktop"
	for sz in 16 32 48 64 128 256 512; do \
		rm -f "$(HOME)/.local/share/icons/hicolor/$${sz}x$${sz}/apps/skyrocket.png"; \
	done

clean:
	rm -f $(TARGET) *.o rsMath/*.o
