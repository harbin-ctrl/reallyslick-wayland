XDG_SHELL_XML := /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
XDG_DECO_XML  := /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml

CC = gcc

# No -march/-mtune here. This used to pin -march=armv8-a -mtune=cortex-a72,
# which is a Pi 4 and nothing else: on an x86_64 box gcc rejects the switch
# outright ("bad value 'armv8-a' for '-march='") and flurry is the one
# screensaver in this repo that will not build. None of its siblings pin an
# architecture, and the tuning bought nothing measurable -- the work happens
# on the GPU, and -O3 -ffast-math is already the part that matters.
CFLAGS = -std=gnu99 -pedantic -Wall -O3 -ffast-math -DHAVE_CONFIG_H -DUSE_GL -I. $(shell pkg-config --cflags wayland-client wayland-egl egl gl)
LIBS = $(shell pkg-config --libs wayland-client wayland-egl egl gl) -lGLU -lm

SRCS = main.c flurry.c flurry-smoke.c flurry-spark.c flurry-star.c flurry-texture.c \
       yarandom.c xdg-shell-client-protocol.c xdg-decoration-unstable-v1-client-protocol.c

OBJS = $(SRCS:.c=.o)

# wayland-scanner output, generated here rather than committed -- it is a
# product of the system's protocol XML, not source. Every other screensaver
# in this repo already generates its own; flurry was the one that did not,
# so a fresh clone had no xdg-shell-client-protocol.h and stopped in main.c
# before reaching a single line of flurry's own code.
HEADERS = xdg-shell-client-protocol.h xdg-decoration-unstable-v1-client-protocol.h

all: flurry

flurry: $(OBJS)
	$(CC) $(CFLAGS) -o flurry $(OBJS) $(LIBS)

# The headers are an order-only prerequisite of every object: they must exist
# before anything compiles, but regenerating one must not force a full rebuild.
%.o: %.c | $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

xdg-shell-client-protocol.h: $(XDG_SHELL_XML)
	wayland-scanner client-header < $< > $@

xdg-shell-client-protocol.c: $(XDG_SHELL_XML)
	wayland-scanner private-code < $< > $@

xdg-decoration-unstable-v1-client-protocol.h: $(XDG_DECO_XML)
	wayland-scanner client-header < $< > $@

xdg-decoration-unstable-v1-client-protocol.c: $(XDG_DECO_XML)
	wayland-scanner private-code < $< > $@

clean:
	rm -f *.o flurry $(HEADERS) \
		xdg-shell-client-protocol.c \
		xdg-decoration-unstable-v1-client-protocol.c
