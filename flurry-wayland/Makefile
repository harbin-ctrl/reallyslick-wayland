CC = gcc
CFLAGS = -std=gnu99 -pedantic -Wall -g -O2 -DHAVE_CONFIG_H -DSTANDALONE -DUSE_GL -DUSE_XFT -I. $(shell pkg-config --cflags x11 xft gl glu egl)
LIBS = $(shell pkg-config --libs x11 xft gl glu egl) -lXt -lSM -lICE -lm

SRCS = colors.c erase-gl.c flurry.c flurry-smoke.c flurry-spark.c flurry-star.c \
       flurry-texture.c font-retry.c fps.c fps-gl.c glsl-utils.c hsv.c pow2.c \
       resources.c screenhack.c texfont.c usleep.c utf8wc.c visual.c visual-gl.c \
       xlock-gl-utils.c xlockmore.c xmu.c yarandom.c

OBJS = $(SRCS:.c=.o)

all: flurry

flurry: $(OBJS)
	$(CC) $(CFLAGS) -o flurry $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o flurry
