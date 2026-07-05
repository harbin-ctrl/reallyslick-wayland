#ifndef DRIVER_H
#define DRIVER_H

#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xstuff {
	unsigned int windowWidth, windowHeight;
} xstuff_t;

int strtol_minmaxdef(const char *optarg, const int base, const int min, const int max, const int type, const int def, const char *errmsg);

#define DRIVER_OPTIONS_LONG {"root", 0, 0, 'r'}, {"maxfps", 1, 0, 'x'}, {"vsync", 1, 0, 'y'}, {"dpms", 1, 0, 'M'},
#define DRIVER_OPTIONS_SHORT "rx:y:M:"
#define DRIVER_OPTIONS_HELP "\t--root/-r\n" "\t--maxfps/-x <arg>\n" "\t--vsync/-y <arg>\n" "\t--dpms/-M <arg>\n"
#define DRIVER_OPTIONS_CASES case 'r': case 'x': case 'y': case 'M': /* ignored */ break;

void hack_handle_opts (int argc, char **argv);
void hack_init (xstuff_t *);
void hack_reshape (xstuff_t *);
void hack_draw (xstuff_t *, double, float);
void hack_cleanup (xstuff_t *);

#ifdef __cplusplus
}
#endif 

#endif
