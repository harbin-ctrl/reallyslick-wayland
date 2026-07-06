CC = gcc
CFLAGS = -std=gnu99 -pedantic -Wall -g -O2 -DHAVE_CONFIG_H -DUSE_GL -I. $(shell pkg-config --cflags wayland-client wayland-egl egl gl)
LIBS = $(shell pkg-config --libs wayland-client wayland-egl egl gl) -lGLU -lm

SRCS = main.c flurry.c flurry-smoke.c flurry-spark.c flurry-star.c flurry-texture.c \
       yarandom.c xdg-shell-client-protocol.c xdg-decoration-unstable-v1-client-protocol.c

OBJS = $(SRCS:.c=.o)

all: flurry

flurry: $(OBJS)
	$(CC) $(CFLAGS) -o flurry $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o flurry
