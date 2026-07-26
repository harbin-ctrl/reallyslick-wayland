#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#include "driver.h"
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-client-protocol.h"

int strtol_minmaxdef(const char *optarg, const int base, const int min, const int max, const int type, const int def, const char *errmsg) {
    if (!optarg) return def;
    int val = strtol(optarg, NULL, base);
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

/* Wayland variables */
static struct wl_display             *g_display;
static struct wl_compositor          *g_compositor;
static struct xdg_wm_base            *g_wm_base;
static struct wl_surface             *g_surface;
static struct xdg_surface            *g_xdg_surface;
static struct xdg_toplevel           *g_toplevel;
static struct wl_seat                *g_seat;
static struct wl_keyboard            *g_keyboard;
static struct wl_egl_window          *g_egl_window;
static struct zxdg_decoration_manager_v1   *g_deco_manager  = NULL;
static struct zxdg_toplevel_decoration_v1  *g_toplevel_deco = NULL;

static int g_fullscreen = 0;
static int g_start_fullscreen = 0;
static struct wl_callback *g_frame_callback = NULL;
static bool g_benchmark_mode = false;

static EGLDisplay  g_egl_display = EGL_NO_DISPLAY;
static EGLContext  g_egl_context = EGL_NO_CONTEXT;
static EGLSurface  g_egl_surface = EGL_NO_SURFACE;
static EGLConfig   g_egl_config;

static int g_configured   = 0;
static int g_running      = 1;
static int g_needs_resize = 0;
static int g_new_width, g_new_height;

/* App specific variables */
static unsigned int g_win_width  = 800;
static unsigned int g_win_height = 600;
static GLuint accumTex = 0;
static bool texValid = false;
static xstuff_t xstuff;
static int currentPreset = 1;
static char presetBuf[10];
static char *fake_argv[] = { (char*)"solarwinds", (char*)"-R", presetBuf, NULL };

static void render_frame(); /* forward declaration */

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
        xstuff.windowWidth = g_win_width;
        xstuff.windowHeight = g_win_height;
        hack_reshape(&xstuff);

        glBindTexture(GL_TEXTURE_2D, accumTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, g_win_width, g_win_height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        texValid = false;

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

static void kb_keymap(void *d, struct wl_keyboard *kb, uint32_t fmt, int fd, uint32_t sz) { close(fd); }
static void kb_enter(void *d, struct wl_keyboard *kb, uint32_t ser, struct wl_surface *s, struct wl_array *k) {}
static void kb_leave(void *d, struct wl_keyboard *kb, uint32_t ser, struct wl_surface *s) {}
static void kb_key(void *d, struct wl_keyboard *kb, uint32_t ser, uint32_t t, uint32_t key, uint32_t state) {
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return;
    if (key == 1 || key == 16) {  /* KEY_ESC or KEY_Q */
        g_running = 0;
    } else if (key == 87 || key == 33) {  /* KEY_F11 or KEY_F */
        if (g_fullscreen)
            xdg_toplevel_unset_fullscreen(g_toplevel);
        else
            xdg_toplevel_set_fullscreen(g_toplevel, NULL);
    } else if (key == 105 && !g_benchmark_mode) {  /* KEY_LEFT */
        currentPreset = currentPreset - 1;
        if (currentPreset < 1) currentPreset = 6;
        sprintf(presetBuf, "%d", currentPreset);
        hack_cleanup(&xstuff);
        hack_handle_opts(3, fake_argv);
        hack_init(&xstuff);
        texValid = false;
    } else if (key == 106 && !g_benchmark_mode) {  /* KEY_RIGHT */
        currentPreset = (currentPreset % 6) + 1;
        sprintf(presetBuf, "%d", currentPreset);
        hack_cleanup(&xstuff);
        hack_handle_opts(3, fake_argv);
        hack_init(&xstuff);
        texValid = false;
    }
}
static void kb_modifiers(void *d, struct wl_keyboard *kb, uint32_t ser, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t grp) {}
static void kb_repeat_info(void *d, struct wl_keyboard *kb, int32_t rate, int32_t delay) {}
static const struct wl_keyboard_listener keyboard_lst = {
    kb_keymap, kb_enter, kb_leave, kb_key, kb_modifiers, kb_repeat_info
};

static void seat_capabilities(void *d, struct wl_seat *seat, uint32_t caps) {
    if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
        g_keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(g_keyboard, &keyboard_lst, NULL);
    }
}
static void seat_name(void *d, struct wl_seat *seat, const char *name) {}
static const struct wl_seat_listener seat_lst = { seat_capabilities, seat_name };

static void registry_global(void *d, struct wl_registry *reg, uint32_t name, const char *iface, uint32_t ver) {
    if (!strcmp(iface, wl_compositor_interface.name))
        g_compositor = (struct wl_compositor *)wl_registry_bind(reg, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, xdg_wm_base_interface.name)) {
        g_wm_base = (struct xdg_wm_base *)wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(g_wm_base, &wm_base_listener, NULL);
    } else if (!strcmp(iface, wl_seat_interface.name)) {
        g_seat = (struct wl_seat *)wl_registry_bind(reg, name, &wl_seat_interface, 5);
        wl_seat_add_listener(g_seat, &seat_lst, NULL);
    } else if (!strcmp(iface, zxdg_decoration_manager_v1_interface.name)) {
        g_deco_manager = (struct zxdg_decoration_manager_v1 *)wl_registry_bind(reg, name, &zxdg_decoration_manager_v1_interface, 1);
    }
}
static void registry_remove(void *d, struct wl_registry *r, uint32_t n) {}
static const struct wl_registry_listener registry_lst = { registry_global, registry_remove };

static void deco_configure(void *data, struct zxdg_toplevel_decoration_v1 *deco, uint32_t mode) {}
static const struct zxdg_toplevel_decoration_v1_listener deco_listener = { deco_configure };

static int init_egl() {
    EGLint major, minor;
    g_egl_display = eglGetDisplay((EGLNativeDisplayType)g_display);
    if (g_egl_display == EGL_NO_DISPLAY || !eglInitialize(g_egl_display, &major, &minor)) return 0;
    if (!eglBindAPI(EGL_OPENGL_API)) return 0;

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
        if (!eglChooseConfig(g_egl_display, fallback, &g_egl_config, 1, &n) || n == 0) return 0;
    }
    EGLint ctx_attribs[] = { EGL_NONE };
    g_egl_context = eglCreateContext(g_egl_display, g_egl_config, EGL_NO_CONTEXT, ctx_attribs);
    return (g_egl_context != EGL_NO_CONTEXT);
}

static int create_egl_surface() {
    g_egl_window = wl_egl_window_create(g_surface, g_win_width, g_win_height);
    if (!g_egl_window) return 0;
    g_egl_surface = eglCreateWindowSurface(g_egl_display, g_egl_config, (EGLNativeWindowType)g_egl_window, NULL);
    if (g_egl_surface == EGL_NO_SURFACE || !eglMakeCurrent(g_egl_display, g_egl_surface, g_egl_surface, g_egl_context)) return 0;
    eglSwapInterval(g_egl_display, 0);
    return 1;
}

static void frame_done(void *, struct wl_callback *, uint32_t);
static const struct wl_callback_listener frame_listener = { frame_done };

static struct {
    struct timespec window_start;
    int    frames;
    double frame_ms_sum;
} g_perf;

static void perf_init() {
    clock_gettime(CLOCK_MONOTONIC, &g_perf.window_start);
    g_perf.frames = 0;
    g_perf.frame_ms_sum = 0.0;
}

static void render_frame() {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    // Delta time calculation
    static struct timespec last_time;
    static bool has_last_time = false;
    float real_dt = 1.0f / 60.0f;
    if (has_last_time) {
        real_dt = (t0.tv_sec - last_time.tv_sec) + (t0.tv_nsec - last_time.tv_nsec) * 1e-9;
        if (real_dt <= 0.0f) real_dt = 1.0f / 60.0f;
        if (real_dt > 0.1f) real_dt = 0.1f;
    } else {
        has_last_time = true;
    }
    last_time = t0;

    // Restore accumulation buffer from last frame
    if (texValid) {
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0, 1, 0, 1, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, accumTex);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glDisable(GL_BLEND);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 1.0f);
        glEnd();
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND); // Restore blend state

        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
    } else {
        glClear(GL_COLOR_BUFFER_BIT);
    }

    // hack_draw applies blur quad and draws new particles
    hack_draw(&xstuff, 0.0, real_dt);

    // Save new frame to accumulation texture
    glBindTexture(GL_TEXTURE_2D, accumTex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, g_win_width, g_win_height);
    texValid = true;

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double frame_ms = ((t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9) * 1000.0;

    g_perf.frames++;
    g_perf.frame_ms_sum += frame_ms;

    double elapsed = (t1.tv_sec - g_perf.window_start.tv_sec) + (t1.tv_nsec - g_perf.window_start.tv_nsec) * 1e-9;
    if (g_benchmark_mode && elapsed >= 1.0) {
        fprintf(stderr, "FPS: %5.1f  frame: %5.2f ms\n", g_perf.frames / elapsed, g_perf.frame_ms_sum / g_perf.frames);
        perf_init();
    }

    if (!g_benchmark_mode) {
        if (g_frame_callback) wl_callback_destroy(g_frame_callback);
        g_frame_callback = wl_surface_frame(g_surface);
        wl_callback_add_listener(g_frame_callback, &frame_listener, NULL);
    }
    eglSwapBuffers(g_egl_display, g_egl_surface);
}

static void frame_done(void *, struct wl_callback *cb, uint32_t) {
    wl_callback_destroy(cb);
    g_frame_callback = NULL;
    if (!g_running) return;

    if (g_needs_resize) {
        wl_egl_window_resize(g_egl_window, g_new_width, g_new_height, 0, 0);
        g_win_width  = g_new_width;
        g_win_height = g_new_height;
        g_needs_resize = 0;
        xstuff.windowWidth = g_win_width;
        xstuff.windowHeight = g_win_height;
        hack_reshape(&xstuff);

        glBindTexture(GL_TEXTURE_2D, accumTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, g_win_width, g_win_height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        texValid = false;
    }

    render_frame();
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--fullscreen") || !strcmp(argv[i], "-fullscreen")) {
            g_start_fullscreen = 1;
        }
        else if (!strcmp(argv[i], "--benchmark") || !strcmp(argv[i], "-benchmark")) {
            g_benchmark_mode = true;
        }
    }

    g_display = wl_display_connect(NULL);
    if (!g_display) {
        fprintf(stderr, "solarwinds: cannot connect to Wayland display\n");
        return 1;
    }

    struct wl_registry *registry = wl_display_get_registry(g_display);
    wl_registry_add_listener(registry, &registry_lst, NULL);
    wl_display_roundtrip(g_display);

    if (!g_compositor || !g_wm_base) {
        fprintf(stderr, "solarwinds: missing required Wayland globals\n");
        return 1;
    }

    g_surface = wl_compositor_create_surface(g_compositor);
    g_xdg_surface = xdg_wm_base_get_xdg_surface(g_wm_base, g_surface);
    xdg_surface_add_listener(g_xdg_surface, &xdg_surface_lst, NULL);
    g_toplevel = xdg_surface_get_toplevel(g_xdg_surface);
    xdg_toplevel_add_listener(g_toplevel, &toplevel_lst, NULL);

    xdg_toplevel_set_title(g_toplevel, "Solar Winds");
    xdg_toplevel_set_app_id(g_toplevel, "solarwinds");

    if (g_deco_manager) {
        g_toplevel_deco = zxdg_decoration_manager_v1_get_toplevel_decoration(g_deco_manager, g_toplevel);
        zxdg_toplevel_decoration_v1_add_listener(g_toplevel_deco, &deco_listener, NULL);
        zxdg_toplevel_decoration_v1_set_mode(g_toplevel_deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    if (g_start_fullscreen) {
        xdg_toplevel_set_fullscreen(g_toplevel, NULL);
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

    xstuff.windowWidth = g_win_width;
    xstuff.windowHeight = g_win_height;

    sprintf(presetBuf, "%d", currentPreset);
    hack_handle_opts(3, fake_argv);
    hack_init(&xstuff);

    // Setup accumulation texture for double buffering fix
    glGenTextures(1, &accumTex);
    glBindTexture(GL_TEXTURE_2D, accumTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, g_win_width, g_win_height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    texValid = false;

    perf_init();

    if (g_benchmark_mode) {
        int benchmark_frames = 0;
        struct timespec bench_start;
        clock_gettime(CLOCK_MONOTONIC, &bench_start);
        
        while (g_running && benchmark_frames < 1000) {
            wl_display_dispatch_pending(g_display);
            render_frame();
            benchmark_frames++;
        }
        
        struct timespec bench_end;
        clock_gettime(CLOCK_MONOTONIC, &bench_end);
        float totalSeconds = (bench_end.tv_sec - bench_start.tv_sec) + (bench_end.tv_nsec - bench_start.tv_nsec) * 1e-9;
        printf("Benchmark completed: 1000 frames in %.2f seconds (%.2f FPS)\n", totalSeconds, 1000.0f / totalSeconds);
    } else {
        render_frame();
        while (g_running) {
            if (wl_display_dispatch(g_display) < 0) break;
        }
    }

    /* Cleanup */
    if (g_frame_callback) {
        wl_callback_destroy(g_frame_callback);
        g_frame_callback = NULL;
    }
    glDeleteTextures(1, &accumTex);
    hack_cleanup(&xstuff);

    eglMakeCurrent(g_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(g_egl_display, g_egl_surface);
    wl_egl_window_destroy(g_egl_window);
    eglDestroyContext(g_egl_display, g_egl_context);
    eglTerminate(g_egl_display);

    if (g_toplevel_deco) zxdg_toplevel_decoration_v1_destroy(g_toplevel_deco);
    if (g_deco_manager)  zxdg_decoration_manager_v1_destroy(g_deco_manager);
    if (g_keyboard) wl_keyboard_destroy(g_keyboard);
    xdg_toplevel_destroy(g_toplevel);
    xdg_surface_destroy(g_xdg_surface);
    wl_surface_destroy(g_surface);
    wl_display_disconnect(g_display);

    return 0;
}
