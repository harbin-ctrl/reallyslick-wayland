#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/time.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>

#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glu.h>
#include <linux/input.h>

#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "flurry.h"

// Forward Declarations
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial);
static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial);
static void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
                                   int32_t width, int32_t height, struct wl_array *states);
static void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel);
static void toplevel_decoration_configure(void *data, struct zxdg_toplevel_decoration_v1 *decoration, uint32_t mode);

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

static const struct zxdg_toplevel_decoration_v1_listener toplevel_decoration_listener = {
    .configure = toplevel_decoration_configure,
};

// Globals
struct wl_display *display = NULL;
struct wl_registry *registry = NULL;
struct wl_compositor *compositor = NULL;
struct wl_seat *seat = NULL;
struct wl_keyboard *keyboard = NULL;
struct xdg_wm_base *xdg_wm_base = NULL;

struct wl_surface *surface = NULL;
struct xdg_surface *x_surface = NULL;
struct xdg_toplevel *xdg_toplevel = NULL;
struct wl_egl_window *egl_window = NULL;
struct zxdg_decoration_manager_v1 *decoration_manager = NULL;
struct zxdg_toplevel_decoration_v1 *toplevel_decoration = NULL;

EGLDisplay egl_display = EGL_NO_DISPLAY;
EGLConfig egl_config;
EGLContext egl_context = EGL_NO_CONTEXT;
EGLSurface egl_surface = EGL_NO_SURFACE;

int window_width = 800;
int window_height = 600;
bool size_changed = false;
bool is_fullscreen = false;
bool running = true;
bool skip_copy = false;

// Preset config
char *preset_str = "random";
char *progname = "flurry";

const char *presets[] = {
    "water",
    "fire",
    "psychedelic",
    "rgb",
    "binary",
    "classic",
    "insane",
    "random"
};
const int NUM_PRESETS = 8;
bool preset_changed = false;

static int get_current_preset_index(void) {
    for (int i = 0; i < NUM_PRESETS; i++) {
        if (strcmp(preset_str, presets[i]) == 0) {
            return i;
        }
    }
    return 7; // Default to "random"
}

static void toggle_fullscreen(void) {
    if (is_fullscreen) {
        xdg_toplevel_unset_fullscreen(xdg_toplevel);
        is_fullscreen = false;
    } else {
        xdg_toplevel_set_fullscreen(xdg_toplevel, NULL);
        is_fullscreen = true;
    }
    wl_surface_commit(surface);
    wl_display_flush(display);
}

// Keyboard listener
static void keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard,
                            uint32_t format, int32_t fd, uint32_t size) {
    close(fd);
}

static void keyboard_enter(void *data, struct wl_keyboard *wl_keyboard,
                           uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {}

static void keyboard_leave(void *data, struct wl_keyboard *wl_keyboard,
                           uint32_t serial, struct wl_surface *surface) {}

static void keyboard_key(void *data, struct wl_keyboard *wl_keyboard,
                         uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        if (key == KEY_F) {
            toggle_fullscreen();
        } else if (key == KEY_LEFT) {
            int idx = get_current_preset_index();
            idx = (idx - 1 + NUM_PRESETS) % NUM_PRESETS;
            preset_str = (char *)presets[idx];
            printf("Switching preset to: %s\n", preset_str);
            fflush(stdout);
            preset_changed = true;
        } else if (key == KEY_RIGHT) {
            int idx = get_current_preset_index();
            idx = (idx + 1) % NUM_PRESETS;
            preset_str = (char *)presets[idx];
            printf("Switching preset to: %s\n", preset_str);
            fflush(stdout);
            preset_changed = true;
        }
    }
}

static void keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard,
                               uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
                               uint32_t mods_locked, uint32_t group) {}

static void keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
                                 int32_t rate, int32_t delay) {}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

// Seat capabilities listener
static void seat_capabilities(void *data, struct wl_seat *wl_seat, uint32_t capabilities) {
    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        keyboard = wl_seat_get_keyboard(wl_seat);
        wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && keyboard) {
        wl_keyboard_destroy(keyboard);
        keyboard = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *wl_seat, const char *name) {}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

// Registry global listeners
static void registry_global(void *data, struct wl_registry *wl_registry,
                            uint32_t name, const char *interface, uint32_t version) {
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        compositor = wl_registry_bind(wl_registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        xdg_wm_base = wl_registry_bind(wl_registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(xdg_wm_base, &xdg_wm_base_listener, NULL);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        seat = wl_registry_bind(wl_registry, name, &wl_seat_interface, 7);
        wl_seat_add_listener(seat, &seat_listener, NULL);
    } else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
        decoration_manager = wl_registry_bind(wl_registry, name, &zxdg_decoration_manager_v1_interface, 1);
    }
}

static void registry_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

// XDG Shell listeners
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    xdg_surface_ack_configure(xdg_surface, serial);
}

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
                                   int32_t width, int32_t height, struct wl_array *states) {
    printf("xdg_toplevel_configure: %d x %d\n", width, height);
    fflush(stdout);
    if (width > 0 && height > 0) {
        if (window_width != width || window_height != height) {
            window_width = width;
            window_height = height;
            size_changed = true;
        }
    }
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    running = false;
}

static void toplevel_decoration_configure(void *data, struct zxdg_toplevel_decoration_v1 *decoration, uint32_t mode) {
}

GLuint trail_texture = 0;

static void init_trail_texture(int width, int height) {
    if (trail_texture) {
        glDeleteTextures(1, &trail_texture);
        trail_texture = 0;
    }
    
    glGenTextures(1, &trail_texture);
    glBindTexture(GL_TEXTURE_2D, trail_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Clear texture by clearing screen and copying it initially
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("init_trail_texture: glCopyTexSubImage2D failed with error 0x%x\n", err);
        fflush(stdout);
    }
}

int main(int argc, char **argv) {
    if (argc > 1) {
        preset_str = argv[1];
    }

    display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        return -1;
    }

    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    if (!compositor || !xdg_wm_base) {
        fprintf(stderr, "Compositor or XDG shell support missing\n");
        return -1;
    }

    // EGL Setup
    egl_display = eglGetDisplay((EGLNativeDisplayType)display);
    if (egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "Failed to get EGL display\n");
        return -1;
    }

    EGLint major, minor;
    if (!eglInitialize(egl_display, &major, &minor)) {
        fprintf(stderr, "Failed to initialize EGL\n");
        return -1;
    }

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };

    EGLint num_configs;
    if (!eglChooseConfig(egl_display, config_attribs, &egl_config, 1, &num_configs) || num_configs < 1) {
        fprintf(stderr, "Failed to choose EGL config\n");
        return -1;
    }

    eglBindAPI(EGL_OPENGL_API);

    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, NULL);
    if (egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "Failed to create EGL context\n");
        return -1;
    }

    // Create surface and window
    surface = wl_compositor_create_surface(compositor);
    x_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, surface);
    xdg_surface_add_listener(x_surface, &xdg_surface_listener, NULL);

    xdg_toplevel = xdg_surface_get_toplevel(x_surface);
    xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);
    xdg_toplevel_set_title(xdg_toplevel, "Flurry (Wayland)");

    if (is_fullscreen) {
        xdg_toplevel_set_fullscreen(xdg_toplevel, NULL);
    }

    if (decoration_manager) {
        toplevel_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(decoration_manager, xdg_toplevel);
        zxdg_toplevel_decoration_v1_add_listener(toplevel_decoration, &toplevel_decoration_listener, NULL);
        zxdg_toplevel_decoration_v1_set_mode(toplevel_decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    // Commit and roundtrip to initialize configure
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    egl_window = wl_egl_window_create(surface, window_width, window_height);
    egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)egl_window, NULL);
    if (egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "Failed to create EGL window surface\n");
        return -1;
    }



    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        fprintf(stderr, "Failed to make EGL context current\n");
        return -1;
    }

    ModeInfo mi = {
        .width = window_width,
        .height = window_height,
        .screen = 0
    };

    init_trail_texture(window_width, window_height);
    init_flurry(&mi);

    float prev_bbox_min_x = 0.0f;
    float prev_bbox_min_y = 0.0f;
    float prev_bbox_max_x = 0.0f;
    float prev_bbox_max_y = 0.0f;
    bool prev_bbox_valid = false;

    while (running) {
        wl_display_dispatch_pending(display);

        if (size_changed) {
            wl_egl_window_resize(egl_window, window_width, window_height, 0, 0);
            eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
            mi.width = window_width;
            mi.height = window_height;
            reshape_flurry(&mi, window_width, window_height);
            init_trail_texture(window_width, window_height);
            skip_copy = true;
            prev_bbox_valid = false;
            size_changed = false;
        }

        if (preset_changed) {
            reshape_flurry(&mi, window_width, window_height);
            init_trail_texture(window_width, window_height);
            prev_bbox_valid = false;
            preset_changed = false;
        }



        // 1. Draw the previous frame's trails from texture onto the back buffer
        glViewport(0, 0, window_width, window_height);
        
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0, 1, 0, 1, -1, 1);
        
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, trail_texture);
        glDisable(GL_BLEND);

        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 1.0f);
        glEnd();

        glDisable(GL_TEXTURE_2D);

        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);

        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();

        // 2. Render flurry on top (adds new particles and overlays the fade)
        draw_flurry(&mi);

        // 3. Copy the finished frame from back buffer back to the trail texture
        if (!skip_copy) {
            float x_min = 0.0f;
            float y_min = 0.0f;
            float x_max = (float)window_width;
            float y_max = (float)window_height;

            global_info_t *global = flurry_info + MI_SCREEN(&mi);

            if (!global->bbox_empty) {
                float p_min_x = global->bbox_min_x - 64.0f;
                float p_min_y = global->bbox_min_y - 64.0f;
                float p_max_x = global->bbox_max_x + 64.0f;
                float p_max_y = global->bbox_max_y + 64.0f;

                if (prev_bbox_valid) {
                    float shrink_speed = 4.0f;
                    float lag_min_x = prev_bbox_min_x + shrink_speed;
                    float lag_min_y = prev_bbox_min_y + shrink_speed;
                    float lag_max_x = prev_bbox_max_x - shrink_speed;
                    float lag_max_y = prev_bbox_max_y - shrink_speed;

                    if (lag_min_x > lag_max_x) {
                        lag_min_x = p_min_x;
                        lag_max_x = p_max_x;
                    }
                    if (lag_min_y > lag_max_y) {
                        lag_min_y = p_min_y;
                        lag_max_y = p_max_y;
                    }

                    x_min = p_min_x < lag_min_x ? p_min_x : lag_min_x;
                    y_min = p_min_y < lag_min_y ? p_min_y : lag_min_y;
                    x_max = p_max_x > lag_max_x ? p_max_x : lag_max_x;
                    y_max = p_max_y > lag_max_y ? p_max_y : lag_max_y;
                } else {
                    x_min = p_min_x;
                    y_min = p_min_y;
                    x_max = p_max_x;
                    y_max = p_max_y;
                }

                if (x_min < 0.0f) x_min = 0.0f;
                if (y_min < 0.0f) y_min = 0.0f;
                if (x_max > (float)window_width) x_max = (float)window_width;
                if (y_max > (float)window_height) y_max = (float)window_height;

                prev_bbox_min_x = x_min;
                prev_bbox_min_y = y_min;
                prev_bbox_max_x = x_max;
                prev_bbox_max_y = y_max;
                prev_bbox_valid = true;
            } else {
                prev_bbox_valid = false;
                x_min = 0.0f;
                y_min = 0.0f;
                x_max = 0.0f;
                y_max = 0.0f;
            }

            int copy_w = (int)(x_max - x_min);
            int copy_h = (int)(y_max - y_min);

            if (copy_w > 0 && copy_h > 0) {
                glBindTexture(GL_TEXTURE_2D, trail_texture);
                glCopyTexSubImage2D(GL_TEXTURE_2D, 0, (int)x_min, (int)y_min, (int)x_min, (int)y_min, copy_w, copy_h);
            }
        } else {
            skip_copy = false;
            prev_bbox_valid = false;
        }



        // 4. Swap EGL buffers
        if (!eglSwapBuffers(egl_display, egl_surface)) {
            printf("eglSwapBuffers failed: 0x%x\n", eglGetError());
            fflush(stdout);
        }

        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            printf("OpenGL Error: 0x%x\n", err);
            fflush(stdout);
        }
    }

    free_flurry(&mi);

    if (trail_texture) {
        glDeleteTextures(1, &trail_texture);
    }

    eglDestroySurface(egl_display, egl_surface);
    wl_egl_window_destroy(egl_window);
    if (toplevel_decoration) zxdg_toplevel_decoration_v1_destroy(toplevel_decoration);
    xdg_toplevel_destroy(xdg_toplevel);
    xdg_surface_destroy(x_surface);
    wl_surface_destroy(surface);
    if (decoration_manager) zxdg_decoration_manager_v1_destroy(decoration_manager);

    eglDestroyContext(egl_display, egl_context);
    eglTerminate(egl_display);

    if (keyboard) wl_keyboard_destroy(keyboard);
    if (seat) wl_seat_destroy(seat);
    if (xdg_wm_base) xdg_wm_base_destroy(xdg_wm_base);
    if (compositor) wl_compositor_destroy(compositor);
    if (registry) wl_registry_destroy(registry);
    wl_display_disconnect(display);

    return 0;
}
