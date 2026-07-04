/*
 * Substrate screensaver - Wayland/EGL port
 * Original code by Jared Tarbell (2004) and Mike Kershaw (2004)
 * Wayland/EGL modern GL port: standalone Wayland client, no X11.
 *
 * Build:  make
 * Run:    ./substrate [options]
 * Quit:   Escape or Q or close window
 * Keys:   F / F11 = toggle fullscreen, Left / Right arrow / Space = restart generation
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <vector>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-client-protocol.h"

#define STEP 0.42f

/* Raw colormap extracted from pollockEFF.gif */
static const char *rgb_colormap[] = {
    "#201F21", "#262C2E", "#352626", "#372B27",
    "#302C2E", "#392B2D", "#323229", "#3F3229",
    "#38322E", "#2E333D", "#333A3D", "#473329",
    "#40392C", "#40392E", "#47402C", "#47402E",
    "#4E402C", "#4F402E", "#4E4738", "#584037",
    "#65472D", "#6D5D3D", "#745530", "#755532",
    "#745D32", "#746433", "#7C6C36", "#523152",
    "#444842", "#4C5647", "#655D45", "#6D5D44",
    "#6C5D4E", "#746C43", "#7C6C42", "#7C6C4B",
    "#6B734B", "#73734B", "#7B7B4A", "#6B6C55",
    "#696D5E", "#7B6C5D", "#6B7353", "#6A745D",
    "#727B52", "#7B7B52", "#57746E", "#687466",
    "#9C542B", "#9D5432", "#9D5B35", "#936B36",
    "#AA7330", "#C45A27", "#D95223", "#D85A20",
    "#DB5A23", "#E57037", "#836C4B", "#8C6B4B",
    "#82735C", "#937352", "#817B63", "#817B6D",
    "#927B63", "#D9893B", "#E49832", "#DFA133",
    "#E5A037", "#F0AB3B", "#8A8A59", "#B29A58",
    "#89826B", "#9A8262", "#888B7C", "#909A7A",
    "#A28262", "#A18A69", "#A99968", "#99A160",
    "#99A168", "#CA8148", "#EB8D43", "#C29160",
    "#C29168", "#D1A977", "#C9B97F", "#F0E27B",
    "#9F928B", "#C0B999", "#E6B88F", "#C8C187",
    "#E0C886", "#F2CC85", "#F5DA83", "#ECDE9D",
    "#F5D294", "#F5DA94", "#F4E784", "#F4E18A",
    "#F4E193", "#E7D8A7", "#F1D4A5", "#F1DCA5",
    "#F4DBAD", "#F1DCAE", "#F4DBB5", "#F5DBBD",
    "#F4E2AD", "#F5E9AD", "#F4E3BE", "#F5EABE",
    "#F7F0B6", "#D9D1C1", "#E0D0C0", "#E7D8C0",
    "#F1DDC6", "#E8E1C0", "#F3EDC7", "#F6ECCE",
    "#F8F2C7", "#EFEFD0", nullptr
};

struct crack {
    float x, y;
    float t;
    float dx, dy;
    float ys, xs, t_inc; /* for curvature calculations */
    int curved;
    uint32_t sandcolor;
    float sandp, sandg;
    float degrees_drawn;
    int crack_num;
};

struct field {
    unsigned int height;
    unsigned int width;
    unsigned int initial_cracks;
    unsigned int num;
    unsigned int max_num;
    int grains; /* number of grains in the sand painting */
    int circle_percent;
    crack *cracks; /* array of cracks */
    int *cgrid; /* grid of actual crack placement */
    // uint32_t *off_img; removed
    int numcolors;
    uint32_t *parsedcolors;
    uint32_t fgcolor;
    uint32_t bgcolor;
    unsigned int cycles;
    unsigned int wireframe;
    unsigned int seamless;
};

/* --- Global Saver Settings --- */
static bool dWireframe = false;
static bool dSeamless = false;
static bool g_is_dark_bg = false;
static unsigned int dMaxCycles = 10000;
static unsigned int dInitialCracks = 3;
static unsigned int dMaxCracks = 100;
static int dSandGrains = 64;
static int dCirclePercent = 33;
static const char *dBgColorStr = "white";
static const char *dFgColorStr = "black";
static float dSpeed = 0.5f;

static field *g_field = nullptr;

/* --- OpenGL state and function pointers --- */
static GLuint g_prog = 0;
static GLint  g_u_samp = -1;
static GLuint g_vbo = 0;
static GLuint g_ibo = 0;
static GLuint g_tex_id = 0;

typedef void (*PFN_GenBuffers_t)   (GLsizei, GLuint *);
typedef void (*PFN_BindBuffer_t)   (GLenum, GLuint);
typedef void (*PFN_BufferData_t)   (GLenum, ptrdiff_t, const void *, GLenum);
typedef void (*PFN_DeleteBuffers_t)(GLsizei, const GLuint *);

static PFNGLCREATESHADERPROC              fn_CreateShader          = nullptr;
static PFNGLSHADERSOURCEPROC              fn_ShaderSource          = nullptr;
static PFNGLCOMPILESHADERPROC             fn_CompileShader         = nullptr;
static PFNGLGETSHADERIVPROC               fn_GetShaderiv           = nullptr;
static PFNGLGETSHADERINFOLOGPROC          fn_GetShaderInfoLog      = nullptr;
static PFNGLCREATEPROGRAMPROC             fn_CreateProgram         = nullptr;
static PFNGLATTACHSHADERPROC              fn_AttachShader          = nullptr;
static PFNGLBINDATTRIBLOCATIONPROC        fn_BindAttribLocation    = nullptr;
static PFNGLLINKPROGRAMPROC               fn_LinkProgram           = nullptr;
static PFNGLGETPROGRAMIVPROC              fn_GetProgramiv          = nullptr;
static PFNGLGETPROGRAMINFOLOGPROC         fn_GetProgramInfoLog     = nullptr;
static PFNGLUSEPROGRAMPROC                fn_UseProgram            = nullptr;
static PFNGLGETUNIFORMLOCATIONPROC        fn_GetUniformLocation    = nullptr;
static PFNGLUNIFORM1IPROC                 fn_Uniform1i             = nullptr;
static PFNGLENABLEVERTEXATTRIBARRAYPROC   fn_EnableVertexAttribArray  = nullptr;
static PFNGLDISABLEVERTEXATTRIBARRAYPROC  fn_DisableVertexAttribArray = nullptr;
static PFNGLVERTEXATTRIBPOINTERPROC       fn_VertexAttribPointer   = nullptr;
static PFNGLDELETESHADERPROC              fn_DeleteShader          = nullptr;
static PFNGLDELETEPROGRAMPROC             fn_DeleteProgram         = nullptr;

static PFN_GenBuffers_t    fn_GenBuffers    = nullptr;
static PFN_BindBuffer_t    fn_BindBuffer    = nullptr;
static PFN_BufferData_t    fn_BufferData    = nullptr;
static PFN_DeleteBuffers_t fn_DeleteBuffers = nullptr;

static PFNGLGENFRAMEBUFFERSPROC fn_GenFramebuffers = nullptr;
static PFNGLBINDFRAMEBUFFERPROC fn_BindFramebuffer = nullptr;
static PFNGLFRAMEBUFFERTEXTURE2DPROC fn_FramebufferTexture2D = nullptr;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC fn_CheckFramebufferStatus = nullptr;
static PFNGLDELETEFRAMEBUFFERSPROC fn_DeleteFramebuffers = nullptr;
typedef void (APIENTRY * PFNGLUNIFORM2FPROC) (GLint location, GLfloat v0, GLfloat v1);
static PFNGLUNIFORM2FPROC fn_Uniform2f = nullptr;

static GLuint g_fbo = 0;
static GLuint g_point_prog = 0;
static GLuint g_point_vbo = 0;
#include <vector>
static std::vector<float> g_point_buffer;

static void load_gl_functions() {
    fn_CreateShader         = (PFNGLCREATESHADERPROC)            eglGetProcAddress("glCreateShader");
    fn_ShaderSource         = (PFNGLSHADERSOURCEPROC)            eglGetProcAddress("glShaderSource");
    fn_CompileShader        = (PFNGLCOMPILESHADERPROC)           eglGetProcAddress("glCompileShader");
    fn_GetShaderiv          = (PFNGLGETSHADERIVPROC)             eglGetProcAddress("glGetShaderiv");
    fn_GetShaderInfoLog     = (PFNGLGETSHADERINFOLOGPROC)        eglGetProcAddress("glGetShaderInfoLog");
    fn_CreateProgram        = (PFNGLCREATEPROGRAMPROC)           eglGetProcAddress("glCreateProgram");
    fn_AttachShader         = (PFNGLATTACHSHADERPROC)            eglGetProcAddress("glAttachShader");
    fn_BindAttribLocation   = (PFNGLBINDATTRIBLOCATIONPROC)      eglGetProcAddress("glBindAttribLocation");
    fn_LinkProgram          = (PFNGLLINKPROGRAMPROC)             eglGetProcAddress("glLinkProgram");
    fn_GetProgramiv         = (PFNGLGETPROGRAMIVPROC)            eglGetProcAddress("glGetProgramiv");
    fn_GetProgramInfoLog    = (PFNGLGETPROGRAMINFOLOGPROC)       eglGetProcAddress("glGetProgramInfoLog");
    fn_UseProgram           = (PFNGLUSEPROGRAMPROC)              eglGetProcAddress("glUseProgram");
    fn_GetUniformLocation   = (PFNGLGETUNIFORMLOCATIONPROC)      eglGetProcAddress("glGetUniformLocation");
    fn_Uniform1i            = (PFNGLUNIFORM1IPROC)               eglGetProcAddress("glUniform1i");
    fn_EnableVertexAttribArray  = (PFNGLENABLEVERTEXATTRIBARRAYPROC) eglGetProcAddress("glEnableVertexAttribArray");
    fn_DisableVertexAttribArray = (PFNGLDISABLEVERTEXATTRIBARRAYPROC)eglGetProcAddress("glDisableVertexAttribArray");
    fn_VertexAttribPointer  = (PFNGLVERTEXATTRIBPOINTERPROC)     eglGetProcAddress("glVertexAttribPointer");
    fn_DeleteShader         = (PFNGLDELETESHADERPROC)            eglGetProcAddress("glDeleteShader");
    fn_DeleteProgram        = (PFNGLDELETEPROGRAMPROC)           eglGetProcAddress("glDeleteProgram");

    fn_GenBuffers    = (PFN_GenBuffers_t)   eglGetProcAddress("glGenBuffers");
    fn_BindBuffer    = (PFN_BindBuffer_t)   eglGetProcAddress("glBindBuffer");
    fn_BufferData    = (PFN_BufferData_t)   eglGetProcAddress("glBufferData");
    fn_DeleteBuffers = (PFN_DeleteBuffers_t)eglGetProcAddress("glDeleteBuffers");
    fn_GenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)eglGetProcAddress("glGenFramebuffers");
    fn_BindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)eglGetProcAddress("glBindFramebuffer");
    fn_FramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)eglGetProcAddress("glFramebufferTexture2D");
    fn_CheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)eglGetProcAddress("glCheckFramebufferStatus");
    fn_DeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)eglGetProcAddress("glDeleteFramebuffers");
    fn_Uniform2f = (PFNGLUNIFORM2FPROC)eglGetProcAddress("glUniform2f");
}

/* --- Helper Math / Utility --- */
static uint32_t fast_rand_seed = 123456789;
static inline uint32_t fast_rand() {
    uint32_t x = fast_rand_seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    fast_rand_seed = x;
    return x;
}
#define rand fast_rand
static inline float frand(float max) {
    return (float)(fast_rand() & 0xFFFFFF) / (float)0xFFFFFF * max;
}

static inline void point2rgb(uint32_t c, int *r, int *g, int *b) {
    *r = c & 0xFF;
    *g = (c >> 8) & 0xFF;
    *b = (c >> 16) & 0xFF;
}

static inline uint32_t rgb2point(int r, int g, int b, int a = 255) {
    return (a << 24) | (b << 16) | (g << 8) | r;
}

static inline uint32_t hsv_to_rgb(float h, float s, float v) {
    float c = v * s;
    float h_prime = h / 60.0f;
    float h_prime_mod_2 = h_prime;
    while (h_prime_mod_2 >= 2.0f) h_prime_mod_2 -= 2.0f;
    float x = c * (1.0f - fabsf(h_prime_mod_2 - 1.0f));
    float m = v - c;
    float r = 0, g = 0, b = 0;
    if (h_prime < 1.0f) { r = c; g = x; b = 0; }
    else if (h_prime < 2.0f) { r = x; g = c; b = 0; }
    else if (h_prime < 3.0f) { r = 0; g = c; b = x; }
    else if (h_prime < 4.0f) { r = 0; g = x; b = c; }
    else if (h_prime < 5.0f) { r = x; g = 0; b = c; }
    else { r = c; g = 0; b = x; }
    return rgb2point((int)((r + m) * 255.0f), (int)((g + m) * 255.0f), (int)((b + m) * 255.0f));
}

static uint32_t parse_hex_color(const char *str) {
    if (str[0] == '#') str++;
    unsigned int r = 0, g = 0, b = 0;
    if (sscanf(str, "%02x%02x%02x", &r, &g, &b) == 3) {
        return rgb2point(r, g, b);
    }
    return 0;
}

static uint32_t parse_color(const char *name) {
    if (strcasecmp(name, "white") == 0) return rgb2point(255, 255, 255);
    if (strcasecmp(name, "black") == 0) return rgb2point(0, 0, 0);
    if (name[0] == '#') {
        return parse_hex_color(name);
    }
    return rgb2point(0, 0, 0);
}

/* --- Wayland + EGL Boilerplate State --- */
static struct wl_display    *g_display;
static struct wl_compositor *g_compositor;
static struct xdg_wm_base   *g_wm_base;
static struct wl_surface    *g_surface;
static struct xdg_surface   *g_xdg_surface;
static struct xdg_toplevel  *g_toplevel;
static struct wl_seat       *g_seat;
static struct wl_keyboard   *g_keyboard = nullptr;
static struct wl_pointer    *g_pointer = nullptr;
static struct wl_shm        *g_shm = nullptr;
static struct wl_surface    *g_cursor_surface = nullptr;
static struct wl_buffer     *g_cursor_buffer = nullptr;
static uint32_t             *g_cursor_data = nullptr;
static uint32_t              g_cursor_serial = 0;
static struct wl_egl_window                *g_egl_window;

static void update_cursor_color();
static void setup_cursor();
static struct zxdg_decoration_manager_v1   *g_deco_manager  = nullptr;
static struct zxdg_toplevel_decoration_v1  *g_toplevel_deco = nullptr;

static int g_fullscreen = 0;
static int g_start_fullscreen = 0;
static struct wl_callback *g_frame_callback = nullptr;
static bool g_benchmark_mode = false;

static EGLDisplay  g_egl_display = EGL_NO_DISPLAY;
static EGLContext  g_egl_context = EGL_NO_CONTEXT;
static EGLSurface  g_egl_surface = EGL_NO_SURFACE;
static EGLConfig   g_egl_config;

static int g_win_width    = 800;
static int g_win_height   = 600;
static int g_configured   = 0;
static int g_running      = 1;
static int g_needs_resize = 0;
static int g_new_width, g_new_height;

class rsTimer {
    struct timespec last;
public:
    rsTimer() { clock_gettime(CLOCK_MONOTONIC, &last); }
    double tick() {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double dt = (now.tv_sec - last.tv_sec) +
                    (now.tv_nsec - last.tv_nsec) * 1e-9;
        last = now;
        return dt;
    }
};

static const float SIM_DT      = 1.0f / 60.0f;
static float       g_sim_accum = 0.0f;
static rsTimer     g_wall_timer;

/* --- Substrate Core Logic --- */
static bool g_force_render = true;
static inline uint32_t trans_point(int x, int y, uint32_t myc, float a, field *f) {
    if (x >= 0 && x < (int)f->width && y >= 0 && y < (int)f->height) {
        int r, g, b;
        point2rgb(myc, &r, &g, &b);
        g_point_buffer.push_back((float)x);
        g_point_buffer.push_back((float)y);
        g_point_buffer.push_back(r / 255.0f);
        g_point_buffer.push_back(g / 255.0f);
        g_point_buffer.push_back(b / 255.0f);
        g_point_buffer.push_back(a);
    }
    return myc;
}

static inline void start_crack(struct field *f, crack *cr);
static inline void make_crack(struct field *f);

static inline void region_color(struct field *f, crack *cr) {
    float rx = cr->x;
    float ry = cr->y;
    int openspace = 1;
    int cx, cy;
    float maxg;
    int grains, i;
    float w;
    float drawx, drawy;

    float rx_step = 0.81f * sinf(cr->t * M_PI / 180.0f);
    float ry_step = 0.81f * cosf(cr->t * M_PI / 180.0f);
    while (openspace) {
        /* move perpendicular to crack */
        rx += rx_step;
        ry -= ry_step;

        cx = (int) rx;
        cy = (int) ry;
        if (f->seamless) {
            cx = (cx % (int)f->width + (int)f->width) % (int)f->width;
            cy = (cy % (int)f->height + (int)f->height) % (int)f->height;
        }

        if ((cx >= 0) && (cx < (int)f->width) && (cy >= 0) && (cy < (int)f->height)) {
            if (f->cgrid[cy * f->width + cx] > 10000) {
                /* space is open */
            } else {
                openspace = 0;
            }
        } else {
            openspace = 0;
        }
    }

    cr->sandg += (frand(0.1f) - 0.050f);
    maxg = 1.0f;
    if (cr->sandg < 0) cr->sandg = 0;
    if (cr->sandg > maxg) cr->sandg = maxg;

    grains = f->grains;
    w = cr->sandg / (grains - 1);

    for (i = 0; i < grains; i++) {
        float sval = sinf(cr->sandp + sinf((float) i * w));
        drawx = cr->x + (rx - cr->x) * sval;
        drawy = cr->y + (ry - cr->y) * sval;
        if (f->seamless) {
            if (drawx >= f->width) drawx -= f->width;
            else if (drawx < 0.0f) drawx += f->width;
            if (drawy >= f->height) drawy -= f->height;
            else if (drawy < 0.0f) drawy += f->height;
        }

        trans_point((int)drawx, (int)drawy, cr->sandcolor, (0.1f - (float)i / (grains * 10.0f)), f);
    }
}

static inline void start_crack(struct field *f, crack *cr) {
    int px = 0;
    int py = 0;
    int found = 0;
    int timeout = 0;
    float a;

    while ((!found) && (timeout++ < 10000)) {
        px = (int) (rand() % f->width);
        py = (int) (rand() % f->height);

        if (f->cgrid[py * f->width + px] < 10000)
            found = 1;
    }

    if (!found) {
        px = cr->x;
        py = cr->y;

        if (px < 0) px = 0;
        if (px >= (int)f->width) px = f->width - 1;
        if (py < 0) py = 0;
        if (py >= (int)f->height) py = f->height - 1;

        f->cgrid[py * f->width + px] = cr->t;
    }

    a = f->cgrid[py * f->width + px];

    if ((rand() % 100) < 50) {
        a -= 90 + (frand(4.1f) - 2.0f);
    } else {
        a += 90 + (frand(4.1f) - 2.0f);
    }

    if ((rand() % 100) < f->circle_percent) {
        float r;
        float radian_inc;

        cr->curved = 1;
        cr->degrees_drawn = 0;
        r = 10 + (rand() % ((f->width + f->height) / 2));
        if ((rand() % 100) < 50) {
            r *= -1;
        }

        radian_inc = STEP / r;
        cr->t_inc = radian_inc * 360.0f / 2.0f / M_PI;

        cr->ys = r * sinf(radian_inc);
        cr->xs = r * (1.0f - cosf(radian_inc));
    } else {
        cr->curved = 0;
    }

    cr->x = px + (0.61f * cosf(a * M_PI / 180.0f));
    cr->y = py + (0.61f * sinf(a * M_PI / 180.0f));
    cr->t = a;
    cr->dx = STEP * cosf(a * M_PI / 180.0f);
    cr->dy = STEP * sinf(a * M_PI / 180.0f);
}

static inline void make_crack(struct field *f) {
    crack *cr;

    if (f->num < f->max_num) {
        f->cracks = (crack *) realloc(f->cracks, sizeof(crack) * (f->num + 1));
        cr = &(f->cracks[f->num]);
        cr->sandp = 0;
        cr->sandg = (frand(0.2f) - 0.01f);
        cr->sandcolor = f->parsedcolors[rand() % f->numcolors];
        cr->crack_num = f->num;
        cr->curved = 0;
        cr->degrees_drawn = 0;

        cr->x = rand() % f->width;
        cr->y = rand() % f->height;
        cr->t = rand() % 360;

        start_crack(f, cr);
        f->num++;
    }
}

static void build_substrate(struct field *f) {
    f->cycles = 0;
    if (f->cgrid) {
        free(f->cgrid);
        f->cgrid = nullptr;
    }
    if (f->cracks) {
        free(f->cracks);
        f->cracks = nullptr;
    }
    f->num = 0;

    f->cgrid = (int *) malloc(sizeof(int) * f->height * f->width);
    for (unsigned int j = 0; j < f->height * f->width; j++) {
        f->cgrid[j] = 10001;
    }

    for (int tx = 0; tx < (int)f->initial_cracks; tx++)
        make_crack(f);
}

static void clear_img(field *f) {
    if (g_fbo == 0) return;
    int r, g, b;
    point2rgb(f->bgcolor, &r, &g, &b);
    fn_BindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glClearColor(r/255.0f, g/255.0f, b/255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    fn_BindFramebuffer(GL_FRAMEBUFFER, 0);
    g_force_render = true;
}

static inline void movedrawcrack(struct field *f, int cracknum) {
    int cx, cy;
    crack *cr = &(f->cracks[cracknum]);

    if (!cr->curved) {
        cr->x += cr->dx;
        cr->y += cr->dy;
    } else {
        cr->x += (cr->ys * cosf(cr->t * M_PI / 180.0f));
        cr->y += (cr->ys * sinf(cr->t * M_PI / 180.0f));

        cr->x += (cr->xs * cosf(cr->t * M_PI / 180.0f - M_PI / 2.0f));
        cr->y += (cr->xs * sinf(cr->t * M_PI / 180.0f - M_PI / 2.0f));

        cr->t += cr->t_inc;
        cr->degrees_drawn += fabsf(cr->t_inc);
    }
    if (f->seamless) {
        if (cr->x >= f->width) cr->x -= f->width;
        else if (cr->x < 0.0f) cr->x += f->width;
        if (cr->y >= f->height) cr->y -= f->height;
        else if (cr->y < 0.0f) cr->y += f->height;
    }

    cx = (int)(cr->x + (frand(0.66f) - 0.33f));
    cy = (int)(cr->y + (frand(0.66f) - 0.33f));
    if (f->seamless) {
        cx = (cx % (int)f->width + (int)f->width) % (int)f->width;
        cy = (cy % (int)f->height + (int)f->height) % (int)f->height;
    }

    if ((cx >= 0) && (cx < (int)f->width) && (cy >= 0) && (cy < (int)f->height)) {
        if (!f->wireframe)
            region_color(f, cr);

        int r, g, b;
        point2rgb(f->fgcolor, &r, &g, &b);
        g_point_buffer.push_back((float)cx);
        g_point_buffer.push_back((float)cy);
        g_point_buffer.push_back(r / 255.0f);
        g_point_buffer.push_back(g / 255.0f);
        g_point_buffer.push_back(b / 255.0f);
        g_point_buffer.push_back(1.0f);

        if (cr->curved && (cr->degrees_drawn > 360.0f)) {
            start_crack(f, cr);
            make_crack(f);
        } else if ((f->cgrid[cy * f->width + cx] > 10000) ||
                   (fabsf(f->cgrid[cy * f->width + cx] - cr->t) < 5.0f)) {
            f->cgrid[cy * f->width + cx] = (int) cr->t;
        } else if (fabsf(f->cgrid[cy * f->width + cx] - cr->t) > 2.0f) {
            start_crack(f, cr);
            make_crack(f);
        }
    } else {
        cr->x = rand() % f->width;
        cr->y = rand() % f->height;
        cr->t = rand() % 360;

        start_crack(f, cr);
        make_crack(f);
    }
}

/* --- GL Setup Quad and Texture --- */
static void setup_quad_geometry() {
    float verts[] = {
        // pos.x, pos.y,  tc.x, tc.y
        -1.0f, -1.0f,    0.0f, 1.0f,
         1.0f, -1.0f,    1.0f, 1.0f,
         1.0f,  1.0f,    1.0f, 0.0f,
        -1.0f,  1.0f,    0.0f, 0.0f
    };
    unsigned int indices[] = {
        0, 1, 2,
        2, 3, 0
    };

    if (g_vbo == 0) fn_GenBuffers(1, &g_vbo);
    fn_BindBuffer(GL_ARRAY_BUFFER, g_vbo);
    fn_BufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    if (g_ibo == 0) fn_GenBuffers(1, &g_ibo);
    fn_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);
    fn_BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
}

static void setup_fbo(int w, int h) {
    if (!g_fbo) fn_GenFramebuffers(1, &g_fbo);
    fn_BindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    fn_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_tex_id, 0);
    fn_BindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void build_point_shader_program() {
    const char *VERT_SRC =
        "#version 140\n"
        "in vec2 a_pos;\n"
        "in vec4 a_color;\n"
        "out vec4 v_color;\n"
        "uniform vec2 u_res;\n"
        "void main() {\n"
        "    vec2 ndc = (a_pos + 0.5) / u_res * 2.0 - 1.0;\n"
        "    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
        "    v_color = a_color;\n"
        "}\n";

    const char *FRAG_SRC =
        "#version 140\n"
        "in vec4 v_color;\n"
        "out vec4 frag;\n"
        "void main() {\n"
        "    frag = v_color;\n"
        "}\n";

    auto compile = [](GLenum type, const char *src) -> GLuint {
        GLuint s = fn_CreateShader(type);
        fn_ShaderSource(s, 1, &src, nullptr);
        fn_CompileShader(s);
        return s;
    };
    GLuint vs = compile(GL_VERTEX_SHADER,   VERT_SRC);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FRAG_SRC);
    g_point_prog = fn_CreateProgram();
    fn_AttachShader(g_point_prog, vs);
    fn_AttachShader(g_point_prog, fs);
    fn_BindAttribLocation(g_point_prog, 0, "a_pos");
    fn_BindAttribLocation(g_point_prog, 1, "a_color");
    fn_LinkProgram(g_point_prog);
    fn_DeleteShader(vs);
    fn_DeleteShader(fs);
    if (!g_point_vbo) fn_GenBuffers(1, &g_point_vbo);
}

static void flush_points() {
    if (g_point_buffer.empty()) return;
    fn_BindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    fn_UseProgram(g_point_prog);
    if (fn_Uniform2f) {
        GLint res_loc = fn_GetUniformLocation(g_point_prog, "u_res");
        fn_Uniform2f(res_loc, (float)g_field->width, (float)g_field->height);
    }
    
    fn_BindBuffer(GL_ARRAY_BUFFER, g_point_vbo);
    fn_BufferData(GL_ARRAY_BUFFER, g_point_buffer.size() * sizeof(float), g_point_buffer.data(), GL_STREAM_DRAW);
    
    fn_EnableVertexAttribArray(0);
    fn_EnableVertexAttribArray(1);
    fn_VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    fn_VertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
    
    glDrawArrays(GL_POINTS, 0, g_point_buffer.size() / 6);
    
    fn_DisableVertexAttribArray(0);
    fn_DisableVertexAttribArray(1);
    fn_UseProgram(0);
    fn_BindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_BLEND);
    
    g_point_buffer.clear();
}

static void setup_texture(int w, int h) {
    if (g_tex_id == 0) {
        glGenTextures(1, &g_tex_id);
    }
    glBindTexture(GL_TEXTURE_2D, g_tex_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Allocate texture storage (NULL data initially)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
}

static void build_shader_program() {
    const char *VERT_SRC =
        "#version 140\n"
        "in vec2 a_pos;\n"
        "in vec2 a_tc;\n"
        "out vec2 v_tc;\n"
        "void main() {\n"
        "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
        "    v_tc = a_tc;\n"
        "}\n";

    const char *FRAG_SRC =
        "#version 140\n"
        "in vec2 v_tc;\n"
        "uniform sampler2D u_samp;\n"
        "out vec4 frag;\n"
        "void main() {\n"
        "    frag = texture(u_samp, v_tc);\n"
        "}\n";

    auto compile = [](GLenum type, const char *src) -> GLuint {
        GLuint s = fn_CreateShader(type);
        fn_ShaderSource(s, 1, &src, nullptr);
        fn_CompileShader(s);
        GLint ok; fn_GetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512]; fn_GetShaderInfoLog(s, 512, nullptr, log);
            fprintf(stderr, "substrate shader compile error: %s\n", log);
        }
        return s;
    };

    GLuint vs = compile(GL_VERTEX_SHADER,   VERT_SRC);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FRAG_SRC);

    if (g_prog) fn_DeleteProgram(g_prog);
    g_prog = fn_CreateProgram();
    fn_AttachShader(g_prog, vs);
    fn_AttachShader(g_prog, fs);
    fn_BindAttribLocation(g_prog, 0, "a_pos");
    fn_BindAttribLocation(g_prog, 1, "a_tc");
    fn_LinkProgram(g_prog);

    GLint ok; fn_GetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512]; fn_GetProgramInfoLog(g_prog, 512, nullptr, log);
        fprintf(stderr, "substrate link error: %s\n", log);
    }
    fn_DeleteShader(vs);
    fn_DeleteShader(fs);

    fn_UseProgram(g_prog);
    g_u_samp = fn_GetUniformLocation(g_prog, "u_samp");
    fn_Uniform1i(g_u_samp, 0); // unit 0
}

static void initSaver() {
    g_field = new field();
    g_field->width = g_win_width;
    g_field->height = g_win_height;
    g_field->initial_cracks = dInitialCracks;
    g_field->max_num = dMaxCracks;
    g_field->wireframe = dWireframe ? 1 : 0;
    g_field->grains = dSandGrains;
    g_field->circle_percent = dCirclePercent;
    g_field->seamless = dSeamless ? 1 : 0;
    g_field->bgcolor = parse_color(dBgColorStr);
    g_field->fgcolor = parse_color(dFgColorStr);
    int r, g, b;
    point2rgb(g_field->bgcolor, &r, &g, &b);
    g_is_dark_bg = ((r + g + b) / 3 < 128);
    g_field->cycles = 0;
    g_field->cracks = nullptr;
    g_field->cgrid = nullptr;

    int num_map_colors = 0;
    while (rgb_colormap[num_map_colors] != nullptr) {
        num_map_colors++;
    }
    g_field->numcolors = num_map_colors;
    g_field->parsedcolors = new uint32_t[g_field->numcolors];
    for (int i = 0; i < g_field->numcolors; i++) {
        g_field->parsedcolors[i] = parse_hex_color(rgb_colormap[i]);
    }

    g_field->cgrid = new int[g_field->width * g_field->height];

    setup_quad_geometry();
    setup_texture(g_field->width, g_field->height);
    setup_fbo(g_field->width, g_field->height);
    build_shader_program();
    build_point_shader_program();
    glViewport(0, 0, g_field->width, g_field->height);

    build_substrate(g_field);
    clear_img(g_field);
    update_cursor_color();
}

static void handle_resize(int w, int h) {
    if (w == (int)g_field->width && h == (int)g_field->height) return;

    g_field->width = w;
    g_field->height = h;

    delete[] g_field->cgrid;

    g_field->cgrid = new int[w * h];

    setup_texture(w, h);
    setup_fbo(w, h);
    glViewport(0, 0, w, h);

    build_substrate(g_field);
    clear_img(g_field);
}

static void cleanup_saver() {
    if (g_prog) fn_DeleteProgram(g_prog);
    if (g_point_prog) fn_DeleteProgram(g_point_prog);
    if (g_vbo) fn_DeleteBuffers(1, &g_vbo);
    if (g_point_vbo) fn_DeleteBuffers(1, &g_point_vbo);
    if (g_ibo) fn_DeleteBuffers(1, &g_ibo);
    if (g_fbo) fn_DeleteFramebuffers(1, &g_fbo);
    if (g_tex_id) glDeleteTextures(1, &g_tex_id);

    if (g_field) {
        if (g_field->cgrid) delete[] g_field->cgrid;
        if (g_field->cracks) free(g_field->cracks);
        if (g_field->parsedcolors) delete[] g_field->parsedcolors;
        delete g_field;
        g_field = nullptr;
    }
}

/* --- Simulation Update and Telemetry Render Frame --- */
static void update_physics() {
    if (g_field->cycles >= dMaxCycles && dMaxCycles != 0) {
        build_substrate(g_field);
        clear_img(g_field);
    }

    for (unsigned int tempx = 0; tempx < g_field->num; tempx++) {
        movedrawcrack(g_field, tempx);
    }

    g_field->cycles++;
    flush_points();
}

static void render_scene() {
    // Clear backbuffer (even if we cover it, nice to do)
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Bind the texture that was rendered to by the FBO
    glBindTexture(GL_TEXTURE_2D, g_tex_id);

    fn_UseProgram(g_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_tex_id);
    fn_Uniform1i(g_u_samp, 0);

    fn_EnableVertexAttribArray(0); // a_pos
    fn_EnableVertexAttribArray(1); // a_tc

    fn_BindBuffer(GL_ARRAY_BUFFER, g_vbo);
    fn_VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    fn_VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    fn_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

    fn_DisableVertexAttribArray(0);
    fn_DisableVertexAttribArray(1);
}

static void render_frame();

/* --- Wayland / EGL Window System Scaffolding --- */
static void wm_base_ping(void *data, struct xdg_wm_base *base, uint32_t serial) {
    xdg_wm_base_pong(base, serial);
}
static const struct xdg_wm_base_listener wm_base_listener = { wm_base_ping };

static void xdg_surface_configure(void *data, struct xdg_surface *surf, uint32_t serial) {
    xdg_surface_ack_configure(surf, serial);
    g_configured = 1;
    if (g_egl_window && g_needs_resize) {
        wl_egl_window_resize(g_egl_window, g_new_width, g_new_height, 0, 0);
        g_win_width  = g_new_width;
        g_win_height = g_new_height;
        g_needs_resize = 0;
        handle_resize(g_win_width, g_win_height);
        render_frame();
    }
}
static const struct xdg_surface_listener xdg_surface_lst = { xdg_surface_configure };

static void toplevel_configure(void *data, struct xdg_toplevel *top,
                                int32_t w, int32_t h, struct wl_array *states)
{
    if (w > 0 && h > 0) {
        g_new_width = w; g_new_height = h;
        g_needs_resize = 1;
    }
    g_fullscreen = 0;
    for (uint32_t *st = (uint32_t *)states->data;
         (char *)st < (char *)states->data + states->size; st++) {
        if (*st == XDG_TOPLEVEL_STATE_FULLSCREEN)
            g_fullscreen = 1;
    }
}
static void toplevel_close(void *data, struct xdg_toplevel *top) {
    g_running = 0;
}
static const struct xdg_toplevel_listener toplevel_lst = {
    toplevel_configure, toplevel_close
};

static void kb_keymap(void *d, struct wl_keyboard *kb,
                       uint32_t fmt, int fd, uint32_t sz) { close(fd); }
static void kb_enter(void *d, struct wl_keyboard *kb, uint32_t ser,
                      struct wl_surface *s, struct wl_array *k) {}
static void kb_leave(void *d, struct wl_keyboard *kb, uint32_t ser,
                      struct wl_surface *s) {}
static void kb_key(void *d, struct wl_keyboard *kb, uint32_t ser,
                    uint32_t t, uint32_t key, uint32_t state) {
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return;
    if (key == 1 || key == 16) {  /* KEY_ESC or KEY_Q */
        g_running = 0;
    } else if (key == 87 || key == 33) {  /* KEY_F11 or KEY_F */
        if (g_fullscreen)
            xdg_toplevel_unset_fullscreen(g_toplevel);
        else
            xdg_toplevel_set_fullscreen(g_toplevel, nullptr);
    } else if (key == 105 || key == 106) {  /* KEY_LEFT / KEY_RIGHT */
        g_is_dark_bg = !g_is_dark_bg;
        float base_hue = frand(360.0f);
        if (g_is_dark_bg) {
            g_field->bgcolor = hsv_to_rgb(base_hue, frand(0.5f), frand(0.2f) + 0.05f);
            g_field->fgcolor = hsv_to_rgb(fmodf(base_hue + 180.0f, 360.0f), 0.5f + frand(0.5f), 0.8f + frand(0.2f));
            for (int i = 0; i < g_field->numcolors; i++) {
                float hue = fmodf(base_hue + (360.0f / g_field->numcolors) * i + frand(30.0f), 360.0f);
                g_field->parsedcolors[i] = hsv_to_rgb(hue, 0.6f + frand(0.4f), 0.7f + frand(0.3f));
            }
        } else {
            g_field->bgcolor = hsv_to_rgb(base_hue, frand(0.3f), 0.8f + frand(0.2f));
            g_field->fgcolor = hsv_to_rgb(fmodf(base_hue + 180.0f, 360.0f), 0.5f + frand(0.5f), frand(0.3f));
            for (int i = 0; i < g_field->numcolors; i++) {
                float hue = fmodf(base_hue + (360.0f / g_field->numcolors) * i + frand(30.0f), 360.0f);
                g_field->parsedcolors[i] = hsv_to_rgb(hue, 0.7f + frand(0.3f), frand(0.4f));
            }
        }
        build_substrate(g_field);
        clear_img(g_field);
        update_cursor_color();
    } else if (key == 57 || key == 28 || key == 96) {  /* KEY_SPACE / KEY_ENTER / KEY_KPENTER */
        g_field->bgcolor = parse_color(dBgColorStr);
        g_field->fgcolor = parse_color(dFgColorStr);
        for (int i = 0; i < g_field->numcolors; i++) {
            g_field->parsedcolors[i] = parse_hex_color(rgb_colormap[i]);
        }
        int r, g, b;
        point2rgb(g_field->bgcolor, &r, &g, &b);
        g_is_dark_bg = ((r + g + b) / 3 < 128);
        build_substrate(g_field);
        clear_img(g_field);
        update_cursor_color();
    }
}
static void kb_modifiers(void *d, struct wl_keyboard *kb, uint32_t ser,
                          uint32_t dep, uint32_t lat, uint32_t lock, uint32_t grp) {}
static void kb_repeat_info(void *d, struct wl_keyboard *kb,
                            int32_t rate, int32_t delay) {}
static const struct wl_keyboard_listener keyboard_lst = {
    kb_keymap, kb_enter, kb_leave, kb_key, kb_modifiers, kb_repeat_info
};

static void update_cursor_color() {
    if (!g_cursor_data) return;
    uint32_t color = g_is_dark_bg ? 0xFFFFFFFF : 0xFF000000;
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            if ((x - 8)*(x - 8) + (y - 8)*(y - 8) <= 4) {
                g_cursor_data[y * 16 + x] = color;
            } else {
                g_cursor_data[y * 16 + x] = 0x00000000;
            }
        }
    }
    if (g_cursor_surface) {
        wl_surface_attach(g_cursor_surface, g_cursor_buffer, 0, 0);
        wl_surface_damage(g_cursor_surface, 0, 0, 16, 16);
        wl_surface_commit(g_cursor_surface);
        if (g_pointer) {
            wl_pointer_set_cursor(g_pointer, g_cursor_serial, g_cursor_surface, 8, 8);
        }
    }
}

static void setup_cursor() {
    if (!g_shm || !g_compositor) return;
    int size = 16 * 16 * 4;
    char name[] = "/tmp/substrate-shm-XXXXXX";
    int fd = mkstemp(name);
    if (fd < 0) return;
    unlink(name);
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return;
    }
    g_cursor_data = (uint32_t*)mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    struct wl_shm_pool *pool = wl_shm_create_pool(g_shm, fd, size);
    g_cursor_buffer = wl_shm_pool_create_buffer(pool, 0, 16, 16, 16*4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    g_cursor_surface = wl_compositor_create_surface(g_compositor);
    wl_surface_attach(g_cursor_surface, g_cursor_buffer, 0, 0);
    update_cursor_color();
}

static void pointer_enter(void *d, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t sx, wl_fixed_t sy) {
    g_cursor_serial = serial;
    if (g_cursor_surface) {
        wl_pointer_set_cursor(pointer, serial, g_cursor_surface, 8, 8);
    } else {
        wl_pointer_set_cursor(pointer, serial, nullptr, 0, 0);
    }
}
static void pointer_leave(void *d, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface) {}
static void pointer_motion(void *d, struct wl_pointer *pointer,
                           uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {}
static void pointer_button(void *d, struct wl_pointer *pointer,
                           uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state) {}
static void pointer_axis(void *d, struct wl_pointer *pointer,
                         uint32_t time, uint32_t axis, wl_fixed_t value) {}
static void pointer_frame(void *d, struct wl_pointer *pointer) {}
static void pointer_axis_source(void *d, struct wl_pointer *pointer, uint32_t axis_source) {}
static void pointer_axis_stop(void *d, struct wl_pointer *pointer, uint32_t time, uint32_t axis) {}
static void pointer_axis_discrete(void *d, struct wl_pointer *pointer, uint32_t axis, int32_t discrete) {}

static const struct wl_pointer_listener pointer_lst = {
    pointer_enter,
    pointer_leave,
    pointer_motion,
    pointer_button,
    pointer_axis,
    pointer_frame,
    pointer_axis_source,
    pointer_axis_stop,
    pointer_axis_discrete
};

static void seat_capabilities(void *d, struct wl_seat *seat, uint32_t caps) {
    if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
        g_keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(g_keyboard, &keyboard_lst, nullptr);
    }
    if (caps & WL_SEAT_CAPABILITY_POINTER) {
        g_pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(g_pointer, &pointer_lst, nullptr);
    }
}
static void seat_name(void *d, struct wl_seat *seat, const char *name) {}
static const struct wl_seat_listener seat_lst = { seat_capabilities, seat_name };

static void registry_global(void *d, struct wl_registry *reg,
                              uint32_t name, const char *iface, uint32_t ver)
{
    if (!strcmp(iface, wl_compositor_interface.name))
        g_compositor = (struct wl_compositor *)
            wl_registry_bind(reg, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, xdg_wm_base_interface.name)) {
        g_wm_base = (struct xdg_wm_base *)
            wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(g_wm_base, &wm_base_listener, nullptr);
    } else if (!strcmp(iface, wl_seat_interface.name)) {
        g_seat = (struct wl_seat *)
            wl_registry_bind(reg, name, &wl_seat_interface, 5);
        wl_seat_add_listener(g_seat, &seat_lst, nullptr);
    } else if (!strcmp(iface, zxdg_decoration_manager_v1_interface.name)) {
        g_deco_manager = (struct zxdg_decoration_manager_v1 *)
            wl_registry_bind(reg, name, &zxdg_decoration_manager_v1_interface, 1);
    } else if (!strcmp(iface, wl_shm_interface.name)) {
        g_shm = (struct wl_shm *)
            wl_registry_bind(reg, name, &wl_shm_interface, 1);
    }
}
static void registry_remove(void *d, struct wl_registry *r, uint32_t n) {}
static const struct wl_registry_listener registry_lst = {
    registry_global, registry_remove
};

static void deco_configure(void *data, struct zxdg_toplevel_decoration_v1 *deco,
                            uint32_t mode) {}
static const struct zxdg_toplevel_decoration_v1_listener deco_listener = {
    deco_configure
};

static int init_egl() {
    EGLint major, minor;

    g_egl_display = eglGetDisplay((EGLNativeDisplayType)g_display);
    if (g_egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "substrate: eglGetDisplay failed\n");
        return 0;
    }
    if (!eglInitialize(g_egl_display, &major, &minor)) {
        fprintf(stderr, "substrate: eglInitialize failed\n");
        return 0;
    }

    if (!eglBindAPI(EGL_OPENGL_API)) {
        fprintf(stderr, "substrate: eglBindAPI(EGL_OPENGL_API) failed\n");
        return 0;
    }

    EGLint attribs[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_DEPTH_SIZE,      24,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };
    EGLint n;
    if (!eglChooseConfig(g_egl_display, attribs, &g_egl_config, 1, &n) || n == 0) {
        EGLint fallback[] = {
            EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
            EGL_NONE
        };
        if (!eglChooseConfig(g_egl_display, fallback, &g_egl_config, 1, &n) || n == 0) {
            fprintf(stderr, "substrate: no suitable EGL config\n");
            return 0;
        }
    }

    EGLint ctx_attribs[] = { EGL_NONE };
    g_egl_context = eglCreateContext(g_egl_display, g_egl_config,
                                      EGL_NO_CONTEXT, ctx_attribs);
    if (g_egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "substrate: eglCreateContext failed (0x%x)\n", eglGetError());
        return 0;
    }
    return 1;
}

static int create_egl_surface() {
    g_egl_window = wl_egl_window_create(g_surface, g_win_width, g_win_height);
    if (!g_egl_window) {
        fprintf(stderr, "substrate: wl_egl_window_create failed\n");
        return 0;
    }
    g_egl_surface = eglCreateWindowSurface(g_egl_display, g_egl_config,
                                            (EGLNativeWindowType)g_egl_window, nullptr);
    if (g_egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "substrate: eglCreateWindowSurface failed (0x%x)\n", eglGetError());
        return 0;
    }
    if (!eglMakeCurrent(g_egl_display, g_egl_surface, g_egl_surface, g_egl_context)) {
        fprintf(stderr, "substrate: eglMakeCurrent failed\n");
        return 0;
    }
    eglSwapInterval(g_egl_display, 0);  /* frame callbacks own timing; no EGL blocking */
    return 1;
}

static void frame_done(void *, struct wl_callback *, uint32_t);  /* forward */
static const struct wl_callback_listener frame_listener = { frame_done };

static struct {
    struct timespec window_start;
    int    frames;
    int    phys_steps;
    double frame_ms_sum;
} g_perf;

static void perf_init() {
    clock_gettime(CLOCK_MONOTONIC, &g_perf.window_start);
    g_perf.frames = g_perf.phys_steps = 0;
    g_perf.frame_ms_sum = 0.0;
}

static void render_frame() {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    float real_dt = (float)g_wall_timer.tick() * dSpeed;
    g_sim_accum += real_dt;
    if (g_sim_accum > 4.0f * SIM_DT)
        g_sim_accum = 4.0f * SIM_DT;
    int steps = 0;
    bool frame_needs_render = false;
    while (g_sim_accum >= SIM_DT) {
        update_physics();
        g_sim_accum -= SIM_DT;
        ++steps;
    }
    if (steps > 0 || g_force_render) {
        frame_needs_render = true;
        g_force_render = false;
    }

    if (frame_needs_render) {
        render_scene();
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double frame_ms = ((t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9) * 1000.0;

    g_perf.frames++;
    g_perf.phys_steps  += steps;
    g_perf.frame_ms_sum += frame_ms;

    double elapsed = (t1.tv_sec  - g_perf.window_start.tv_sec) +
                     (t1.tv_nsec - g_perf.window_start.tv_nsec) * 1e-9;
    if (elapsed >= 1.0) {
        double fps     = g_perf.frames / elapsed;
        double avg_ms  = g_perf.frame_ms_sum / g_perf.frames;
        double steps_f = (double)g_perf.phys_steps / g_perf.frames;
        fprintf(stderr, "FPS: %5.1f  frame: %5.2f ms  phys steps/frame: %.2f\n",
                fps, avg_ms, steps_f);
        perf_init();
    }

    if (!g_benchmark_mode) {
        if (g_frame_callback) {
            wl_callback_destroy(g_frame_callback);
        }
        g_frame_callback = wl_surface_frame(g_surface);
        wl_callback_add_listener(g_frame_callback, &frame_listener, nullptr);
    }
    if (frame_needs_render || g_benchmark_mode) {
        eglSwapBuffers(g_egl_display, g_egl_surface);
    } else {
        wl_surface_commit(g_surface);
    }
}

static void frame_done(void *, struct wl_callback *cb, uint32_t) {
    wl_callback_destroy(cb);
    g_frame_callback = nullptr;
    if (!g_running) return;

    if (g_needs_resize) {
        wl_egl_window_resize(g_egl_window, g_new_width, g_new_height, 0, 0);
        g_win_width  = g_new_width;
        g_win_height = g_new_height;
        g_needs_resize = 0;
        handle_resize(g_win_width, g_win_height);
    }

    render_frame();
}

int main(int argc, char *argv[]) {
    // Parse custom arguments
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--wireframe")) {
            dWireframe = true;
        } else if (!strcmp(argv[i], "--no-wireframe")) {
            dWireframe = false;
        } else if (!strcmp(argv[i], "--seamless")) {
            dSeamless = true;
        } else if (!strcmp(argv[i], "--no-seamless")) {
            dSeamless = false;
        } else if (!strcmp(argv[i], "--max-cycles") && i + 1 < argc) {
            dMaxCycles = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--initial-cracks") && i + 1 < argc) {
            dInitialCracks = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--max-cracks") && i + 1 < argc) {
            dMaxCracks = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--sand-grains") && i + 1 < argc) {
            dSandGrains = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--circle-percent") && i + 1 < argc) {
            dCirclePercent = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--bg") && i + 1 < argc) {
            dBgColorStr = argv[++i];
        } else if (!strcmp(argv[i], "--fg") && i + 1 < argc) {
            dFgColorStr = argv[++i];
        } else if (!strcmp(argv[i], "--speed") && i + 1 < argc) {
            dSpeed = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--fullscreen") || !strcmp(argv[i], "-fullscreen")) {
            g_start_fullscreen = 1;
        } else if (!strcmp(argv[i], "--benchmark") || !strcmp(argv[i], "-benchmark")) {
            g_benchmark_mode = true;
        } else {
            fprintf(stderr,
                "Usage: %s [options]\n"
                "Options:\n"
                "  --wireframe | --no-wireframe\n"
                "  --seamless  | --no-seamless\n"
                "  --max-cycles <N>        (default 10000)\n"
                "  --initial-cracks <N>    (default 3)\n"
                "  --max-cracks <N>        (default 100)\n"
                "  --sand-grains <N>       (default 64)\n"
                "  --circle-percent <N>    (default 33)\n"
                "  --bg <color>            (default white)\n"
                "  --fg <color>            (default black)\n"
                "  --speed <float>         (default 0.5)\n"
                "  --fullscreen            \n"
                "  --benchmark             \n",
                argv[0]);
            return 1;
        }
    }

    srand(time(nullptr));
    fast_rand_seed = time(nullptr) ^ 0x1A2B3C4D;

    g_display = wl_display_connect(nullptr);
    if (!g_display) {
        fprintf(stderr, "substrate: cannot connect to Wayland display\n");
        return 1;
    }

    struct wl_registry *registry = wl_display_get_registry(g_display);
    wl_registry_add_listener(registry, &registry_lst, nullptr);
    wl_display_roundtrip(g_display);

    if (!g_compositor || !g_wm_base) {
        fprintf(stderr, "substrate: missing required Wayland globals\n");
        return 1;
    }
    setup_cursor();

    g_surface = wl_compositor_create_surface(g_compositor);
    g_xdg_surface = xdg_wm_base_get_xdg_surface(g_wm_base, g_surface);
    xdg_surface_add_listener(g_xdg_surface, &xdg_surface_lst, nullptr);
    g_toplevel = xdg_surface_get_toplevel(g_xdg_surface);
    xdg_toplevel_add_listener(g_toplevel, &toplevel_lst, nullptr);
    xdg_toplevel_set_title(g_toplevel, "Substrate");
    xdg_toplevel_set_app_id(g_toplevel, "substrate");

    if (g_deco_manager) {
        g_toplevel_deco = zxdg_decoration_manager_v1_get_toplevel_decoration(
            g_deco_manager, g_toplevel);
        zxdg_toplevel_decoration_v1_add_listener(g_toplevel_deco, &deco_listener, nullptr);
        zxdg_toplevel_decoration_v1_set_mode(g_toplevel_deco,
            ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    if (g_start_fullscreen) {
        xdg_toplevel_set_fullscreen(g_toplevel, nullptr);
    }

    wl_surface_commit(g_surface);

    if (!init_egl()) return 1;

    while (!g_configured)
        wl_display_dispatch(g_display);

    if (g_needs_resize) {
        g_win_width  = g_new_width;
        g_win_height = g_new_height;
        g_needs_resize = 0;
    }

    if (!create_egl_surface()) return 1;

    load_gl_functions();
    initSaver();

    perf_init();
    g_wall_timer.tick();

    if (g_benchmark_mode) {
        while (g_running) {
            wl_display_dispatch_pending(g_display);
            render_frame();
        }
    } else {
        render_frame();
        while (g_running) {
            if (wl_display_dispatch(g_display) < 0) break;
        }
    }

    cleanup_saver();

    if (g_frame_callback) {
        wl_callback_destroy(g_frame_callback);
        g_frame_callback = nullptr;
    }
    eglMakeCurrent(g_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(g_egl_display, g_egl_surface);
    wl_egl_window_destroy(g_egl_window);
    eglDestroyContext(g_egl_display, g_egl_context);
    eglTerminate(g_egl_display);
    if (g_toplevel_deco) zxdg_toplevel_decoration_v1_destroy(g_toplevel_deco);
    if (g_deco_manager)  zxdg_decoration_manager_v1_destroy(g_deco_manager);
    if (g_keyboard) wl_keyboard_destroy(g_keyboard);
    if (g_pointer)  wl_pointer_destroy(g_pointer);
    xdg_toplevel_destroy(g_toplevel);
    xdg_surface_destroy(g_xdg_surface);
    wl_surface_destroy(g_surface);
    wl_display_disconnect(g_display);

    return 0;
}
