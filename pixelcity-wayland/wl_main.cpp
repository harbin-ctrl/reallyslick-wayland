/*-----------------------------------------------------------------------------

  wl_main.cpp

  Native Wayland entry point for PixelCity. Replaces the old X11 (Win.cpp) and
  the interim SDL2 (sdl_main.cpp) front-ends with a pure Wayland client:
  wl_compositor + xdg-shell for the window, EGL (desktop GL) for the context,
  and xkbcommon / wl_pointer for input. No SDL, no Xlib.

-----------------------------------------------------------------------------*/

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GL/gl.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cstdint>
#include <ctime>
#include <sys/mman.h>
#include <unistd.h>
#include <poll.h>

#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"

/*--- core entry points (defined across the rest of the program) ------------*/
extern void RandomInit (uint32_t seed);
extern void CameraInit ();
extern void RenderInit ();
extern void TextureInit ();
extern void WorldInit ();

extern void TextureTerm ();
extern void WorldTerm ();
extern void RenderTerm ();
extern void CameraTerm ();

extern void CameraUpdate ();
extern void EntityUpdate ();
extern void WorldUpdate ();
extern void TextureUpdate ();
extern void VisibleUpdate ();
extern void CarUpdate ();
extern void RenderUpdate ();
extern void RenderResize ();

extern bool EntityReady ();

extern void RenderWireframeToggle ();
extern void RenderEffectCycle ();
extern void RenderLetterboxToggle ();
extern void RenderFPSToggle ();
extern void RenderFogToggle ();
extern void RenderFlatToggle ();
extern void RenderHelpToggle ();
extern void WorldReset ();

/*--- app lifecycle ---------------------------------------------------------*/
static void AppInit ()
{
  RandomInit ((uint32_t)time (NULL));
  CameraInit ();
  RenderInit ();
  TextureInit ();
  WorldInit ();
}

static void AppUpdate ()
{
  CameraUpdate ();
  EntityUpdate ();
  WorldUpdate ();
  TextureUpdate ();
  VisibleUpdate ();
  CarUpdate ();
  RenderUpdate ();
}

static void AppTerm ()
{
  TextureTerm ();
  WorldTerm ();
  RenderTerm ();
  CameraTerm ();
}

/*--- window / input state --------------------------------------------------*/
static int              window_width  = 800;
static int              window_height = 600;
static bool             quit          = false;
static bool             is_fullscreen = false;
static int              mouse_x = 0, mouse_y = 0;

static wl_display*      display;
static wl_registry*     registry;
static wl_compositor*   compositor;
static wl_surface*      surface;
static xdg_wm_base*     wm_base;
static xdg_surface*     xsurface;
static xdg_toplevel*    toplevel;
static zxdg_decoration_manager_v1* decoration_manager;
static wl_seat*         seat;
static wl_keyboard*     keyboard;
static wl_pointer*      pointer;
static bool             configured = false;
static bool             resize_pending = false;

static EGLDisplay       egl_display = EGL_NO_DISPLAY;
static EGLContext       egl_context = EGL_NO_CONTEXT;
static EGLSurface       egl_surface = EGL_NO_SURFACE;
static EGLConfig        egl_config;
static wl_egl_window*   egl_window;

static xkb_context*     xkb_ctx;
static xkb_keymap*      xkb_map;
static xkb_state*       xkb_st;

/*--- interface the rest of the program calls -------------------------------*/
int  WinWidth ()  { return window_width; }
int  WinHeight () { return window_height; }
void WinPopup (const char* msg, ...)
{
  va_list ap;
  va_start (ap, msg);
  vfprintf (stderr, msg, ap);
  va_end (ap);
  fputc ('\n', stderr);
}
void WinTerm () { quit = true; }
void WinMousePosition (int* x, int* y) { if (x) *x = mouse_x; if (y) *y = mouse_y; }
// Legacy shims; unused on the Wayland/EGL path but kept for link compatibility.
void* WinGetDisplay () { return NULL; }
uint32_t WinGetWindow () { return 0; }

/*--- xdg-shell / surface listeners -----------------------------------------*/
static void wm_base_ping (void*, xdg_wm_base* b, uint32_t serial)
{
  xdg_wm_base_pong (b, serial);
}
static const xdg_wm_base_listener wm_base_listener = { wm_base_ping };

static void xsurface_configure (void*, xdg_surface* s, uint32_t serial)
{
  xdg_surface_ack_configure (s, serial);
  configured = true;
}
static const xdg_surface_listener xsurface_listener = { xsurface_configure };

static void toplevel_configure (void*, xdg_toplevel*, int32_t w, int32_t h,
                                wl_array*)
{
  if (w > 0 && h > 0 && (w != window_width || h != window_height)) {
    window_width = w;
    window_height = h;
    resize_pending = true;
  }
}
static void toplevel_close (void*, xdg_toplevel*) { quit = true; }
static const xdg_toplevel_listener toplevel_listener = {
  toplevel_configure, toplevel_close, NULL, NULL
};

/*--- keyboard --------------------------------------------------------------*/
static void kb_keymap (void*, wl_keyboard*, uint32_t format, int fd, uint32_t size)
{
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { close (fd); return; }
  char* map = (char*)mmap (NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  close (fd);
  if (map == MAP_FAILED) return;
  xkb_keymap* km = xkb_keymap_new_from_string (xkb_ctx, map,
      XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap (map, size);
  if (!km) return;
  xkb_keymap_unref (xkb_map);
  xkb_state_unref (xkb_st);
  xkb_map = km;
  xkb_st = xkb_state_new (km);
}
static void kb_enter (void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
static void kb_leave (void*, wl_keyboard*, uint32_t, wl_surface*) {}
static void kb_key (void*, wl_keyboard*, uint32_t, uint32_t, uint32_t key,
                    uint32_t state)
{
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED || !xkb_st)
    return;
  xkb_keysym_t sym = xkb_state_key_get_one_sym (xkb_st, key + 8);
  switch (sym) {
  case XKB_KEY_Escape: quit = true; break;
  case XKB_KEY_r: case XKB_KEY_R: WorldReset (); break;
  case XKB_KEY_w: case XKB_KEY_W: RenderWireframeToggle (); break;
  case XKB_KEY_e: case XKB_KEY_E: RenderEffectCycle (); break;
  case XKB_KEY_l: case XKB_KEY_L: RenderLetterboxToggle (); break;
  case XKB_KEY_g: case XKB_KEY_G: RenderFogToggle (); break;
  case XKB_KEY_t: case XKB_KEY_T: RenderFlatToggle (); break;
  case XKB_KEY_p: case XKB_KEY_P: RenderFPSToggle (); break;
  case XKB_KEY_F1: RenderHelpToggle (); break;
  case XKB_KEY_f: case XKB_KEY_F:
    is_fullscreen = !is_fullscreen;
    if (is_fullscreen) xdg_toplevel_set_fullscreen (toplevel, NULL);
    else               xdg_toplevel_unset_fullscreen (toplevel);
    break;
  }
}
static void kb_modifiers (void*, wl_keyboard*, uint32_t, uint32_t dep,
                          uint32_t dla, uint32_t lck, uint32_t grp)
{
  if (xkb_st)
    xkb_state_update_mask (xkb_st, dep, dla, lck, 0, 0, grp);
}
static void kb_repeat (void*, wl_keyboard*, int32_t, int32_t) {}
static const wl_keyboard_listener keyboard_listener = {
  kb_keymap, kb_enter, kb_leave, kb_key, kb_modifiers, kb_repeat
};

/*--- pointer ---------------------------------------------------------------*/
static void pt_enter (void*, wl_pointer*, uint32_t, wl_surface*,
                      wl_fixed_t sx, wl_fixed_t sy)
{
  mouse_x = wl_fixed_to_int (sx);
  mouse_y = wl_fixed_to_int (sy);
}
static void pt_leave (void*, wl_pointer*, uint32_t, wl_surface*) {}
static void pt_motion (void*, wl_pointer*, uint32_t, wl_fixed_t sx, wl_fixed_t sy)
{
  mouse_x = wl_fixed_to_int (sx);
  mouse_y = wl_fixed_to_int (sy);
}
static void pt_button (void*, wl_pointer*, uint32_t, uint32_t, uint32_t, uint32_t) {}
static void pt_axis (void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t) {}
// wl_pointer v5 batches events and adds axis detail. libwayland aborts if any
// listener slot for the bound version is NULL, so stub these even though the
// screensaver camera ignores the mouse.
static void pt_frame (void*, wl_pointer*) {}
static void pt_axis_source (void*, wl_pointer*, uint32_t) {}
static void pt_axis_stop (void*, wl_pointer*, uint32_t, uint32_t) {}
static void pt_axis_discrete (void*, wl_pointer*, uint32_t, int32_t) {}
static const wl_pointer_listener pointer_listener = {
  pt_enter, pt_leave, pt_motion, pt_button, pt_axis,
  pt_frame, pt_axis_source, pt_axis_stop, pt_axis_discrete
};

/*--- seat ------------------------------------------------------------------*/
static void seat_caps (void*, wl_seat* s, uint32_t caps)
{
  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard) {
    keyboard = wl_seat_get_keyboard (s);
    wl_keyboard_add_listener (keyboard, &keyboard_listener, NULL);
  }
  if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer) {
    pointer = wl_seat_get_pointer (s);
    wl_pointer_add_listener (pointer, &pointer_listener, NULL);
  }
}
static void seat_name (void*, wl_seat*, const char*) {}
static const wl_seat_listener seat_listener = { seat_caps, seat_name };

/*--- registry --------------------------------------------------------------*/
static void reg_global (void*, wl_registry* r, uint32_t id,
                        const char* iface, uint32_t ver)
{
  if (!strcmp (iface, wl_compositor_interface.name))
    compositor = (wl_compositor*)wl_registry_bind (r, id, &wl_compositor_interface, ver < 4 ? ver : 4);
  else if (!strcmp (iface, xdg_wm_base_interface.name)) {
    wm_base = (xdg_wm_base*)wl_registry_bind (r, id, &xdg_wm_base_interface, 1);
    xdg_wm_base_add_listener (wm_base, &wm_base_listener, NULL);
  } else if (!strcmp (iface, wl_seat_interface.name)) {
    seat = (wl_seat*)wl_registry_bind (r, id, &wl_seat_interface, ver < 5 ? ver : 5);
    wl_seat_add_listener (seat, &seat_listener, NULL);
  } else if (!strcmp (iface, zxdg_decoration_manager_v1_interface.name)) {
    decoration_manager = (zxdg_decoration_manager_v1*)
        wl_registry_bind (r, id, &zxdg_decoration_manager_v1_interface, 1);
  }
}
static void reg_remove (void*, wl_registry*, uint32_t) {}
static const wl_registry_listener registry_listener = { reg_global, reg_remove };

/*--- EGL -------------------------------------------------------------------*/
static bool init_egl ()
{
  egl_display = eglGetDisplay ((EGLNativeDisplayType)display);
  if (egl_display == EGL_NO_DISPLAY) { fprintf (stderr, "eglGetDisplay failed\n"); return false; }
  if (!eglInitialize (egl_display, NULL, NULL)) { fprintf (stderr, "eglInitialize failed\n"); return false; }
  if (!eglBindAPI (EGL_OPENGL_API)) { fprintf (stderr, "eglBindAPI(OpenGL) failed\n"); return false; }

  const EGLint cfg_attr[] = {
    EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
    EGL_DEPTH_SIZE, 24,
    EGL_NONE
  };
  EGLint n = 0;
  if (!eglChooseConfig (egl_display, cfg_attr, &egl_config, 1, &n) || n < 1) {
    fprintf (stderr, "eglChooseConfig failed\n"); return false;
  }
  egl_context = eglCreateContext (egl_display, egl_config, EGL_NO_CONTEXT, NULL);
  if (egl_context == EGL_NO_CONTEXT) { fprintf (stderr, "eglCreateContext failed\n"); return false; }

  egl_window = wl_egl_window_create (surface, window_width, window_height);
  egl_surface = eglCreateWindowSurface (egl_display, egl_config,
                                        (EGLNativeWindowType)egl_window, NULL);
  if (egl_surface == EGL_NO_SURFACE) { fprintf (stderr, "eglCreateWindowSurface failed\n"); return false; }
  eglMakeCurrent (egl_display, egl_surface, egl_surface, egl_context);
  return true;
}

/*--- non-blocking Wayland event pump --------------------------------------*/
static void pump_events ()
{
  while (wl_display_prepare_read (display) != 0)
    wl_display_dispatch_pending (display);
  wl_display_flush (display);
  pollfd pfd = { wl_display_get_fd (display), POLLIN, 0 };
  if (poll (&pfd, 1, 0) > 0 && (pfd.revents & POLLIN))
    wl_display_read_events (display);
  else
    wl_display_cancel_read (display);
  wl_display_dispatch_pending (display);
}

int main (int argc, char** argv)
{
  bool do_benchmark = false;
  for (int i = 1; i < argc; i++) {
    if (!strcmp (argv[i], "--fullscreen")) is_fullscreen = true;
    if (!strcmp (argv[i], "--benchmark"))  do_benchmark = true;
  }

  display = wl_display_connect (NULL);
  if (!display) { fprintf (stderr, "Cannot connect to a Wayland display.\n"); return 1; }
  xkb_ctx = xkb_context_new (XKB_CONTEXT_NO_FLAGS);

  registry = wl_display_get_registry (display);
  wl_registry_add_listener (registry, &registry_listener, NULL);
  wl_display_roundtrip (display);   // bind globals
  wl_display_roundtrip (display);   // let seat capabilities arrive

  if (!compositor || !wm_base) {
    fprintf (stderr, "Compositor is missing wl_compositor or xdg_wm_base.\n");
    return 1;
  }

  surface  = wl_compositor_create_surface (compositor);
  xsurface = xdg_wm_base_get_xdg_surface (wm_base, surface);
  xdg_surface_add_listener (xsurface, &xsurface_listener, NULL);
  toplevel = xdg_surface_get_toplevel (xsurface);
  xdg_toplevel_add_listener (toplevel, &toplevel_listener, NULL);
  xdg_toplevel_set_title (toplevel, "Pixel City");
  xdg_toplevel_set_app_id (toplevel, "pixelcity");
  // Ask the compositor to draw the window decorations (title bar, borders).
  // Without this, xdg-shell surfaces come up undecorated unless the client
  // draws its own -- which is why the title bar vanished versus the SDL build.
  if (decoration_manager) {
    zxdg_toplevel_decoration_v1* deco =
        zxdg_decoration_manager_v1_get_toplevel_decoration (decoration_manager, toplevel);
    zxdg_toplevel_decoration_v1_set_mode (deco,
        ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
  }
  if (is_fullscreen) xdg_toplevel_set_fullscreen (toplevel, NULL);
  wl_surface_commit (surface);

  while (!configured)               // wait for the first configure
    wl_display_dispatch (display);

  if (!init_egl ()) return 1;
  eglSwapInterval (egl_display, do_benchmark ? 0 : 1);

  AppInit ();

  struct timespec t0; clock_gettime (CLOCK_MONOTONIC, &t0);
  int frames = 0;


  while (!quit) {
    pump_events ();
    if (resize_pending) {
      wl_egl_window_resize (egl_window, window_width, window_height, 0, 0);
      RenderResize ();
      resize_pending = false;
    }
    AppUpdate ();
    eglSwapBuffers (egl_display, egl_surface);

    if (do_benchmark && EntityReady ()) {
      if (frames == 0) clock_gettime (CLOCK_MONOTONIC, &t0);
      frames++;
      if (frames >= 100) {
        struct timespec t1; clock_gettime (CLOCK_MONOTONIC, &t1);
        double dur = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
        printf ("Benchmark completed: 100 frames in %.2f seconds (%.2f FPS)\n",
                dur, 100.0 / dur);
        fflush (stdout);
        quit = true;
      }
    }
  }

  AppTerm ();
  eglMakeCurrent (egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroySurface (egl_display, egl_surface);
  wl_egl_window_destroy (egl_window);
  eglDestroyContext (egl_display, egl_context);
  eglTerminate (egl_display);
  wl_display_disconnect (display);
  return 0;
}
