/*-----------------------------------------------------------------------------

  Render.cpp

  2009 Shamus Young

-------------------------------------------------------------------------------
  
  This is the core of the gl rendering functions.  This contains the main 
  rendering function RenderUpdate (), which initiates the various 
  other renders in the other modules. 

-----------------------------------------------------------------------------*/

#define RENDER_DISTANCE     1280
#define MAX_TEXT            256
#define YOUFAIL(message)    {WinPopup (message);return;}
#define HELP_SIZE           sizeof(help)
#define COLOR_CYCLE_TIME    10000 //milliseconds
#define COLOR_CYCLE         (COLOR_CYCLE_TIME / 4)
#define FONT_COUNT          (sizeof (fonts) / sizeof (struct glFont))
#define FONT_SIZE           (LOGO_PIXELS - LOGO_PIXELS / 8)
// The bloom source is now the composited frame we already drew (captured in
// RenderUpdate). do_effects() squares it via texture combiners so only bright
// pixels bloom; BLOOM_SCALING is the per-pass additive weight over the ~37
// blur quads, tuned for that squared source.
#define BLOOM_SCALING       0.09f

#ifdef WINDOWS
#include <windows.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <math.h>

#include <GL/gl.h>
#include <GL/glu.h>

#ifndef WINDOWS
#include <X11/Xlib.h>
#include <GL/glx.h>

#include <errno.h>
#include <string.h>
#include <stdio.h>
#endif

#include "glTypes.h"
#include "Entity.h"
#include "Car.h"
#include "Camera.h"
#include "Ini.h"
#include "Light.h"
#include "Macro.h"
#include "Math.h"
#include "Render.h"

extern bool generate_icon;
#include "Sky.h"
#include "Texture.h"
#include "gl_ext.h"
#include "Shader.h"
#include "shaders.h"
#include "font_data.h"

GLuint main_fbo = 0;
GLuint main_fbo_tex = 0;
GLuint main_fbo_depth = 0;
CShader* post_shader = NULL;

struct font_data {
  const char* name;
  GLuint tex;
};

#include "World.h"
#include "Win.h"
#include "time_util.h"

#ifdef WINDOWS
static	PIXELFORMATDESCRIPTOR pfd =			
{
	sizeof(PIXELFORMATDESCRIPTOR),			
	1,											  // Version Number
	PFD_DRAW_TO_WINDOW |			// Format Must Support Window
	PFD_SUPPORT_OPENGL |			// Format Must Support OpenGL
	PFD_DOUBLEBUFFER,					// Must Support Double Buffering
	PFD_TYPE_RGBA,						// Request An RGBA Format
	32,										    // Select Our glRgbaDepth
	0, 0, 0, 0, 0, 0,					// glRgbaBits Ignored
	0,											  // No Alpha Buffer
	0,											  // Shift Bit Ignored
	0,											  // Accumulation Buffers
	0, 0, 0, 0,								// Accumulation Bits Ignored
	16,											  // Z-Buffer (Depth Buffer)  bits
	0,											  // Stencil Buffers
	1,											  // Auxiliary Buffers
	PFD_MAIN_PLANE,						// Main Drawing Layer
	0,											  // Reserved
	0, 0, 0										// Layer Masks Ignored
};
#endif

static char             help[] = 
  "ESC - Exit!\n" 
  "F1  - Show this help screen\n" 
  "R   - Rebuild city\n" 
  "L   - Toggle 'letterbox' mode\n"
  "F   - Show Framecounter\n"
  "W   - Toggle Wireframe\n"
  "E   - Change full-scene effects\n"
  "T   - Toggle Textures\n"
  "G   - Toggle Fog\n"
  "S   - Cycle render scale (perf)\n"
#ifndef WINDOWS
  "D   - Toggle Frame Delay\n"
#endif
;

struct glFont
{
  const char*   name;
  unsigned		  base_char;
} fonts[] =
{
  // Bundled OFL display faces (fonts/), loaded by file so the signage looks the
  // same on every machine instead of depending on fontconfig's substitution.
  // A spread of heavy / condensed / techno faces suited to neon city signage.
  {"Anton-Regular.ttf",        0},   // ultra-heavy grotesque (Impact-like)
  {"ArchivoBlack-Regular.ttf", 0},   // heavy grotesque (Arial Black-like)
  {"BebasNeue-Regular.ttf",    0},   // tall condensed caps
  {"RussoOne-Regular.ttf",     0},   // bold squared display
  {"Rajdhani-Bold.ttf",        0},   // squared techno bold
  {"SairaCondensed-Bold.ttf",  0},   // condensed bold
  {"Audiowide-Regular.ttf",    0},   // retro-neon display
};

#if SCREENSAVER
enum
{
  EFFECT_NONE,
  EFFECT_BLOOM,
  EFFECT_BLOOM_RADIAL,
  EFFECT_COLOR_CYCLE,
  EFFECT_GLASS_CITY,
  EFFECT_COUNT,
  EFFECT_DEBUG,
  EFFECT_DEBUG_OVERBLOOM
};
#else
enum
{
  EFFECT_NONE,
  EFFECT_BLOOM,
  EFFECT_COUNT,
  EFFECT_DEBUG_OVERBLOOM,
  EFFECT_DEBUG,
  EFFECT_BLOOM_RADIAL,
  EFFECT_COLOR_CYCLE,
  EFFECT_GLASS_CITY
};
#endif 

#ifdef WINDOWS
static HDC			        hDC;
static HGLRC		        hRC;
#else
// static GLXContext       ctx;
#endif
static float            render_aspect;
static float            fog_distance;
static int              render_width;
static int              render_height;
static int              fbo_width;     // internal 3D render-target size (scaled)
static int              fbo_height;
static float            render_scale = 1.0f; // <1 renders the 3D scene smaller, then upscales
static bool             letterbox;
static int              letterbox_offset;
static int              effect;
// static unsigned         next_fps;  // unused
static unsigned         current_fps;
static unsigned         frames;
static bool             show_wireframe;
static bool             flat;
static bool             show_fps;
static bool             show_fog;
static bool             show_help;

/*-----------------------------------------------------------------------------

  Draw a clock-ish progress.. widget... thing.  It's cute.

-----------------------------------------------------------------------------*/

static void do_progress (float center_x, float center_y, float radius, float opacity, float progress)
{

  int     i;
  int     end_angle;
  float   inner, outer;
  float   angle;
  float   s, c;
  float   gap;

  //Outer Ring
  gap = radius * 0.05f;
  outer = radius;
  inner = radius - gap * 2;
  glColor4f (1,1,1, opacity);
  glBegin (GL_QUAD_STRIP);
  for (i = 0; i <= 360; i+= 15) {
    angle = (float)i * DEGREES_TO_RADIANS;
    s = sinf (angle);
    c = -cosf (angle);
    glVertex2f (center_x + s * outer, center_y + c * outer);
    glVertex2f (center_x + s * inner, center_y + c * inner);
  }
  glEnd ();
  //Progress indicator
  glColor4f (1,1,1, opacity);
  end_angle = (int)(360 * progress);
  outer = radius - gap * 3;
  glBegin (GL_TRIANGLE_FAN);
  glVertex2f (center_x, center_y);
  for (i = 0; i <= end_angle; i+= 3) {
    angle = (float)i * DEGREES_TO_RADIANS;
    s = sinf (angle);
    c = -cosf (angle);
    glVertex2f (center_x + s * outer, center_y + c * outer);
  }
  glEnd ();
  //Tic lines
  glLineWidth (2.0f);
  outer = radius - gap * 1;
  inner = radius - gap * 2;
  glColor4f (0,0,0, opacity);
  glBegin (GL_LINES);
  for (i = 0; i <= 360; i+= 15) {
    angle = (float)i * DEGREES_TO_RADIANS;
    s = sinf (angle);
    c = -cosf (angle);
    glVertex2f (center_x + s * outer, center_y + c * outer);
    glVertex2f (center_x + s * inner, center_y + c * inner);
  }
  glEnd ();

}

/*-----------------------------------------------------------------------------

  Pixellated "PixelCity" title card.

  Rendered as an explicit pixel grid so the blockiness reads as deliberate (it's
  *Pixel* City, after all): each cell is a white square with a 1-unit black gap
  on two sides -- a block:gap ratio of 4:1 -- like an LED sign. We rasterise the
  word once via the normal font, read it back, and collapse it to a coarse
  lit/unlit grid; draw_pixel_title then paints one square per lit cell.

  It serves triple duty: the loading screen, the initial fade-in, and the
  cross-fade into the city.

-----------------------------------------------------------------------------*/

#define TITLE_TEXT        "PixelCity"
#define TITLE_FONT        0            // Anton-Regular: ultra-heavy grotesque
#define TITLE_FADE_IN_MS  900          // title ramps up from black over ~0.9s
#define TITLE_DOWNSAMPLE  2            // font pixels collapsed into one grid cell
#define TITLE_GRID_MAX_W  256
#define TITLE_GRID_MAX_H  96
#define TITLE_BLOCK_FRAC  0.8f         // white block = 4/5 of the cell pitch (4:1)

static unsigned char title_grid[TITLE_GRID_MAX_H][TITLE_GRID_MAX_W]; // 1 = lit cell
static int      title_gw = 0, title_gh = 0;   // grid dims, trimmed to the ink
static int      loading_start_ms = 0;

// Native pixel width of a string in the given baked font (sum of pen advances).
static int title_text_width (int font, const char* s)
{
  int w = 0;
  font %= FONT_COUNT;
  for (; *s; s++) {
    int idx = (unsigned char)*s - font_first_char;
    if (idx >= 0 && idx < font_glyph_count)
      w += (int)font_glyphs[font][idx].xmove;
  }
  return w;
}

// Rasterise TITLE_TEXT once via the font, read it back, and collapse it to a
// coarse lit/unlit cell grid (trimmed to the ink's bounding box).
static void build_title_grid ()
{
  int W = MIN (title_text_width (TITLE_FONT, TITLE_TEXT), render_width);
  int H = MIN (FONT_SIZE + FONT_SIZE / 2, render_height);  // caps + 'y' descender
  if (W < TITLE_DOWNSAMPLE || H < TITLE_DOWNSAMPLE)
    return;

  if (glBindFramebufferEXT)
    glBindFramebufferEXT (GL_FRAMEBUFFER, 0);
  glViewport (0, 0, render_width, render_height);
  glMatrixMode (GL_PROJECTION); glPushMatrix (); glLoadIdentity ();
  glOrtho (0, render_width, render_height, 0, 0.1f, 2048);
  glMatrixMode (GL_MODELVIEW); glPushMatrix (); glLoadIdentity ();
  glTranslatef (0, 0, -1.0f);
  glDisable (GL_DEPTH_TEST); glDepthMask (false);
  glDisable (GL_BLEND); glDisable (GL_TEXTURE_2D); glDisable (GL_FOG);
  glClearColor (0, 0, 0, 1);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Baseline a little above the bottom edge so the whole glyph (incl. the 'y'
  // descender) lands in framebuffer rows [0 .. H].
  RenderPrint (0, render_height - FONT_SIZE / 3, TITLE_FONT, glRgba (1.0f), TITLE_TEXT);

  unsigned char* px = (unsigned char*) malloc ((size_t)W * H * 4);
  if (px) {
    glPixelStorei (GL_PACK_ALIGNMENT, 1);
    glReadPixels (0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);

    int full_gw = MIN (W / TITLE_DOWNSAMPLE, TITLE_GRID_MAX_W);
    int full_gh = MIN (H / TITLE_DOWNSAMPLE, TITLE_GRID_MAX_H);
    int minx = full_gw, miny = full_gh, maxx = -1, maxy = -1;
    // A cell lights if enough of its source block is ink. glReadPixels is
    // bottom-up, so flip rows to put grid row 0 at the top of the letters.
    for (int cy = 0; cy < full_gh; cy++)
      for (int cx = 0; cx < full_gw; cx++) {
        int lit = 0, total = 0;
        for (int dy = 0; dy < TITLE_DOWNSAMPLE; dy++)
          for (int dx = 0; dx < TITLE_DOWNSAMPLE; dx++) {
            int sx = cx * TITLE_DOWNSAMPLE + dx, sy = cy * TITLE_DOWNSAMPLE + dy;
            if (sx >= W || sy >= H) continue;
            total++;
            if (px[((size_t)sy * W + sx) * 4] > 96) lit++;   // red channel
          }
        int on = (total && lit * 100 / total >= 35);
        int gy = full_gh - 1 - cy;                            // flip
        title_grid[gy][cx] = (unsigned char) on;
        if (on) {
          if (cx < minx) minx = cx;
          if (cx > maxx) maxx = cx;
          if (gy < miny) miny = gy;
          if (gy > maxy) maxy = gy;
        }
      }
    free (px);

    // Trim to the ink so the title centres and sizes on the letters, not the
    // empty margins. Reads run ahead of writes (min* >= 0), so this is safe.
    if (maxx >= minx && maxy >= miny) {
      int tw = maxx - minx + 1, th = maxy - miny + 1;
      for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++)
          title_grid[y][x] = title_grid[y + miny][x + minx];
      title_gw = tw;
      title_gh = th;
    }
  }

  glPopMatrix (); glMatrixMode (GL_PROJECTION); glPopMatrix ();
  glMatrixMode (GL_MODELVIEW); glDepthMask (true);
}

// Paint the title, centred, as a grid of white squares with black gaps (4:1),
// additively so the gaps add nothing -- crisp deliberate pixels on the black
// card, and seamless over the veil during the cross-fade. alpha 0..1. Assumes a
// 2D ortho (0,W,H,0) is already active.
static void draw_pixel_title (float alpha)
{
  if (!title_gw || alpha <= 0.0f)
    return;

  // Fit ~60% of the width, but never taller than ~34% of the height.
  float pitch = (0.60f * render_width) / (float)title_gw;
  if (pitch * title_gh > 0.34f * render_height)
    pitch = (0.34f * render_height) / (float)title_gh;
  float block = pitch * TITLE_BLOCK_FRAC;
  float ox = render_width  / 2.0f - pitch * title_gw / 2.0f;
  float oy = render_height / 2.0f - pitch * title_gh / 2.0f;

  glDisable (GL_TEXTURE_2D);
  glEnable (GL_BLEND);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE);           // additive: black gaps add nothing
  glColor4f (1, 1, 1, alpha);
  glBegin (GL_QUADS);
  for (int gy = 0; gy < title_gh; gy++)
    for (int gx = 0; gx < title_gw; gx++)
      if (title_grid[gy][gx]) {
        float x = ox + gx * pitch, y = oy + gy * pitch;
        glVertex2f (x,         y);
        glVertex2f (x,         y + block);
        glVertex2f (x + block, y + block);
        glVertex2f (x + block, y);
      }
  glEnd ();
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

/*-----------------------------------------------------------------------------

  Loading screen: solid black with the pixellated title fading up. Drawn
  directly to the default framebuffer (no FBO / bloom -- those aren't ready yet
  during texture compilation).

-----------------------------------------------------------------------------*/

static void do_loading_screen ()
{

  int now = GetTimeInMillis ();
  if (!title_gw) {
    build_title_grid ();
    loading_start_ms = now;
  }

  // Make sure we're on the default framebuffer, not an FBO.
  if (glBindFramebufferEXT)
    glBindFramebufferEXT (GL_FRAMEBUFFER, 0);

  glViewport (0, 0, render_width, render_height);
  glClearColor (0.0f, 0.0f, 0.0f, 1.0f);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glMatrixMode (GL_PROJECTION);
  glPushMatrix ();
  glLoadIdentity ();
  glOrtho (0, render_width, render_height, 0, 0.1f, 2048);
  glMatrixMode (GL_MODELVIEW);
  glPushMatrix ();
  glLoadIdentity ();
  glTranslatef (0, 0, -1.0f);

  glDisable (GL_DEPTH_TEST);
  glDepthMask (false);
  glDisable (GL_FOG);
  glDisable (GL_CULL_FACE);

  float title_alpha = (float)(now - loading_start_ms) / (float)TITLE_FADE_IN_MS;
  if (title_alpha > 1.0f) title_alpha = 1.0f;
  if (title_alpha < 0.0f) title_alpha = 0.0f;
  draw_pixel_title (title_alpha);

  glPopMatrix ();
  glMatrixMode (GL_PROJECTION);
  glPopMatrix ();
  glMatrixMode (GL_MODELVIEW);
  glDepthMask (true);

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

static void __attribute__((unused)) do_effects (int type)
{

  float           hue1, hue2, hue3, hue4;
  GLrgba          color;
  float           fade;
  int             radius;
  int             x, y;
  int             i;
  int             bloom_radius;
  int             bloom_step;
  float           u, v;
  
  fade = WorldFade ();
  bloom_radius = 15;
  // Wider step -> fewer full-screen bloom quads. Each quad now samples the
  // bloom texture twice (combiner squaring), so trimming the pass count is the
  // main fill-rate lever on the Pi's V3D GPU.
  bloom_step = bloom_radius;
  if (!TextureReady ())
    return;
  //Now change projection modes so we can render full-screen effects
  glMatrixMode (GL_PROJECTION);
  glPushMatrix ();
  glLoadIdentity ();
  glOrtho (0, render_width, render_height, 0, 0.1f, 2048);
	glMatrixMode (GL_MODELVIEW);
  glPushMatrix ();
  glLoadIdentity();
  glTranslatef(0, 0, -1.0f);				
  glDisable (GL_CULL_FACE);
  glDisable (GL_FOG);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  //Render full-screen effects
  glBlendFunc (GL_ONE, GL_ONE);
  glEnable (GL_TEXTURE_2D);
  glDisable(GL_DEPTH_TEST);
  glDepthMask (false);
  glBindTexture(GL_TEXTURE_2D, TextureId (TEXTURE_BLOOM));
  switch (type) {
  case EFFECT_DEBUG:
    glBindTexture(GL_TEXTURE_2D, TextureId (TEXTURE_LOGOS));
    glDisable (GL_BLEND);
    glBegin (GL_QUADS);
    glColor3f (1, 1, 1);
    glTexCoord2f (0, 0);  glVertex2i (0, render_height / 4);
    glTexCoord2f (0, 1);  glVertex2i (0, 0);
    glTexCoord2f (1, 1);  glVertex2i (render_width / 4, 0);
    glTexCoord2f (1, 0);  glVertex2i (render_width / 4, render_height / 4);

    glTexCoord2f (0, 0);  glVertex2i (0, 512);
    glTexCoord2f (0, 1);  glVertex2i (0, 0);
    glTexCoord2f (1, 1);  glVertex2i (512, 0);
    glTexCoord2f (1, 0);  glVertex2i (512, 512);
    glEnd ();
    break;
  case EFFECT_BLOOM_RADIAL:
    //Psychedelic bloom
    glEnable (GL_BLEND);
    glBegin (GL_QUADS);
    color = WorldBloomColor () * BLOOM_SCALING * 2;
    glColor3fv (&color.red);
    for (i = 0; i <= 100; i+=10) {
      glTexCoord2f (0, 0);  glVertex2i (-i, i + render_height);
      glTexCoord2f (0, 1);  glVertex2i (-i, -i);
      glTexCoord2f (1, 1);  glVertex2i (i + render_width, -i);
      glTexCoord2f (1, 0);  glVertex2i (i + render_width, i + render_height);
    }
    glEnd ();
    break;
  case EFFECT_COLOR_CYCLE:
    //Oooh. Pretty colors.  Tint the scene according to screenspace.
    hue1 = (float)(GetTimeInMillis () % COLOR_CYCLE_TIME) / COLOR_CYCLE_TIME;
    hue2 = (float)((GetTimeInMillis () + COLOR_CYCLE) % COLOR_CYCLE_TIME) / COLOR_CYCLE_TIME;
    hue3 = (float)((GetTimeInMillis () + COLOR_CYCLE * 2) % COLOR_CYCLE_TIME) / COLOR_CYCLE_TIME;
    hue4 = (float)((GetTimeInMillis () + COLOR_CYCLE * 3) % COLOR_CYCLE_TIME) / COLOR_CYCLE_TIME;
    glBindTexture(GL_TEXTURE_2D, 0);
    glEnable (GL_BLEND);
    glBlendFunc (GL_ONE, GL_ONE);
    glBlendFunc (GL_DST_COLOR, GL_SRC_COLOR);
    glBegin (GL_QUADS);
    color = glRgbaFromHsl (hue1, 1.0f, 0.6f);
    glColor3fv (&color.red);
    glTexCoord2f (0, 0);  glVertex2i (0, render_height);
    color = glRgbaFromHsl (hue2, 1.0f, 0.6f);
    glColor3fv (&color.red);
    glTexCoord2f (0, 1);  glVertex2i (0, 0);
    color = glRgbaFromHsl (hue3, 1.0f, 0.6f);
    glColor3fv (&color.red);
    glTexCoord2f (1, 1);  glVertex2i (render_width, 0);
    color = glRgbaFromHsl (hue4, 1.0f, 0.6f);
    glColor3fv (&color.red);
    glTexCoord2f (1, 0);  glVertex2i (render_width, render_height);
    glEnd ();
    break;
  case EFFECT_BLOOM:
    //Simple bloom effect. The source is the full composited frame, so square it
    //with the texture combiners (unit0: TEXTURE*TEXTURE = source^2; unit1:
    //*PRIMARY_COLOR = bloom tint * BLOOM_SCALING). Squaring crushes the dark
    //sky/walls toward black while keeping bright windows/lights, so the additive
    //accumulation below only spreads the bright pixels -- no full-frame haze.
    glActiveTexture (GL_TEXTURE0);
    glEnable (GL_TEXTURE_2D);
    glBindTexture (GL_TEXTURE_2D, TextureId (TEXTURE_BLOOM));
    glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    glTexEnvi (GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
    glTexEnvi (GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
    glTexEnvi (GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
    glTexEnvi (GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_TEXTURE);
    glTexEnvi (GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
    glActiveTexture (GL_TEXTURE1);
    glEnable (GL_TEXTURE_2D);
    glBindTexture (GL_TEXTURE_2D, TextureId (TEXTURE_BLOOM));
    glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    glTexEnvi (GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
    glTexEnvi (GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS);
    glTexEnvi (GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
    glTexEnvi (GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
    glTexEnvi (GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
    glActiveTexture (GL_TEXTURE0);
    u = (float)render_width / 2048.0f;
    v = (float)render_height / 2048.0f;
    glBegin (GL_QUADS);
    color = WorldBloomColor () * BLOOM_SCALING;
    glColor3fv (&color.red);
    for (x = -bloom_radius; x <= bloom_radius; x += bloom_step) {
      for (y = -bloom_radius; y <= bloom_radius; y += bloom_step) {
        if (abs (x) == abs (y) && x)
          continue;
        glTexCoord2f (0, 0);  glVertex2i (x, y + render_height);
        glTexCoord2f (0, v);  glVertex2i (x, y);
        glTexCoord2f (u, v);  glVertex2i (x + render_width, y);
        glTexCoord2f (u, 0);  glVertex2i (x + render_width, y + render_height);
      }
    }
    glEnd ();
    // Restore default single-texture modulation.
    glActiveTexture (GL_TEXTURE1);
    glDisable (GL_TEXTURE_2D);
    glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glActiveTexture (GL_TEXTURE0);
    glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    break;
  case EFFECT_DEBUG_OVERBLOOM:
    //This will punish that uppity GPU. Good for testing low frame rate behavior.
    glBegin (GL_QUADS);
    color = WorldBloomColor () * 0.01f;
    glColor3fv (&color.red);
    for (x = -50; x <= 50; x+=5) {
      for (y = -50; y <= 50; y+=5) {
        glTexCoord2f (0, 0);  glVertex2i (x, y + render_height);
        glTexCoord2f (0, 1);  glVertex2i (x, y);
        glTexCoord2f (1, 1);  glVertex2i (x + render_width, y);
        glTexCoord2f (1, 0);  glVertex2i (x + render_width, y + render_height);
      }
    }
    glEnd ();
    break;
  }
  //Do the fade to / from darkness used to hide scene transitions
  if (LOADING_SCREEN) {
    if (fade > 0.0f) {
      glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glEnable (GL_BLEND);
      glDisable (GL_TEXTURE_2D);
      glColor4f (0, 0, 0, fade);
      glBegin (GL_QUADS);
      glVertex2i (0, 0);
      glVertex2i (0, render_height);
      glVertex2i (render_width, render_height);
      glVertex2i (render_width, 0);
      glEnd ();
    }
    if (TextureReady () && !EntityReady () && fade != 0.0f) {
      radius = render_width / 16;
      do_progress ((float)render_width / 2, (float)render_height / 2, (float)radius, fade, EntityProgress ());
      RenderPrint (render_width / 2 - LOGO_PIXELS, render_height / 2 + LOGO_PIXELS, 0, glRgba (0.5f), "%1.2f%%", EntityProgress () * 100.0f);
      RenderPrint (1, "%s v%d.%d.%03d", APP_TITLE, VERSION_MAJOR, VERSION_MINOR, VERSION_REVISION);
    }
  }
  glPopMatrix ();
  glMatrixMode (GL_PROJECTION);
  glPopMatrix ();
  glMatrixMode (GL_MODELVIEW);
  glEnable(GL_DEPTH_TEST);

}


/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

int RenderMaxTextureSize ()
{

  int mts;

  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &mts);
  mts = MIN (mts, render_width);
  return MIN (mts, render_height);

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void RenderPrint (int x, int y, int font, GLrgba color, const char *fmt, ...)				
{

  char		  text[MAX_TEXT];	
  va_list		ap;					
  
  text[0] = 0;
  if (fmt == NULL)			
		  return;						
  va_start(ap, fmt);		
  vsprintf(text, fmt, ap);				
  va_end(ap);		
  glPushAttrib(GL_LIST_BIT);
  glListBase(fonts[font % FONT_COUNT].base_char - font_first_char);
  glColor3fv (&color.red);
	glRasterPos2i (x, y);
  glCallLists(strlen(text), GL_UNSIGNED_BYTE, text);

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void RenderPrint (int line, const char *fmt, ...)				
{

  char		  text[MAX_TEXT];	
	va_list		ap;			
	
  text[0] = 0;	
  if (fmt == NULL)			
		  return;						
  va_start (ap, fmt);		
  vsprintf (text, fmt, ap);				
  va_end (ap);		
  glMatrixMode (GL_PROJECTION);
  glPushMatrix ();
  glLoadIdentity ();
  glOrtho (0, render_width, render_height, 0, 0.1f, 2048);
  glDisable(GL_DEPTH_TEST);
  glDepthMask (false);
	glMatrixMode (GL_MODELVIEW);
  glPushMatrix ();
  glLoadIdentity();
  glTranslatef(0, 0, -1.0f);				
  glDisable (GL_BLEND);
  glDisable (GL_FOG);
  glDisable (GL_TEXTURE_2D);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  RenderPrint (0, line * FONT_SIZE - 2, 0, glRgba (0.0f), text);
  RenderPrint (4, line * FONT_SIZE + 2, 0, glRgba (0.0f), text);
  RenderPrint (2, line * FONT_SIZE, 0, glRgba (1.0f), text);
  glPopAttrib();						
  glPopMatrix ();
  glMatrixMode (GL_PROJECTION);
  glPopMatrix ();
  glMatrixMode (GL_MODELVIEW);

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void static do_help (void)
{

  char*     text;
  int       line;
  char      parse[HELP_SIZE];
  
  strcpy (parse, help);
  line = 0;
  text = strtok (parse, "\n");
  while (text) {
    RenderPrint (line + 2, text);
    text = strtok (NULL, "\n");
    line++;
  }

}


/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void do_fps ()
{

  LIMIT_INTERVAL (1000);
  current_fps = frames;
  frames = 0;

}


/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void RenderResize (void)		
{

  float     fovy;

  render_width = WinWidth ();
  render_height = WinHeight ();
  if (letterbox) {
    letterbox_offset = render_height / 6;
    render_height = render_height - letterbox_offset * 2;
  } else
    letterbox_offset = 0;
  // Internal 3D resolution. Fill-rate (not vertices) is the bottleneck here, so
  // rendering the scene into a smaller FBO and bilinear-upscaling it on the
  // final blit is the cheapest way to buy frame time. Initialised once from
  // $PIXELCITY_SCALE or the saved "RenderScale" percent; cycled live with 'S'.
  {
    static bool scale_init = false;
    if (!scale_init) {
      scale_init = true;
      const char *s = getenv ("PIXELCITY_SCALE");
      if (s)
        render_scale = (float)atof (s);
      else {
        // 85% is the default: it holds 60 fps fullscreen and actually looks
        // better than a razor-sharp 100%. Cycle live with 'S' to taste.
        int pct = IniInt ("RenderScale");
        render_scale = pct > 0 ? pct / 100.0f : 0.85f;
      }
      if (render_scale < 0.25f || render_scale > 1.0f)
        render_scale = 1.0f;
    }
  }
  fbo_width  = MAX (1, (int)(render_width  * render_scale));
  fbo_height = MAX (1, (int)(render_height * render_scale));
  //render_aspect = (float)render_height / (float)render_width;
  glViewport (0, letterbox_offset, render_width, render_height);
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  render_aspect = (float)render_width / (float)render_height;
  fovy = 60.0f;
  if (render_aspect > 1.0f) 
    fovy /= render_aspect; 
  gluPerspective (fovy, render_aspect, 0.1f, RENDER_DISTANCE);
	glMatrixMode (GL_MODELVIEW);

  if (glGenFramebuffersEXT && glFramebufferTexture2DEXT && glBindFramebufferEXT) {
    if (!main_fbo) {
        glGenFramebuffersEXT(1, &main_fbo);
        glGenTextures(1, &main_fbo_tex);
        glGenRenderbuffersEXT(1, &main_fbo_depth);
        if (!post_shader) post_shader = new CShader(bloomVertexShader, bloomFragmentShader);
    }

    glBindFramebufferEXT(GL_FRAMEBUFFER, main_fbo);
    
    glBindTexture(GL_TEXTURE_2D, main_fbo_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, fbo_width, fbo_height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, main_fbo_tex, 0);

    glBindRenderbufferEXT(GL_RENDERBUFFER, main_fbo_depth);
    glRenderbufferStorageEXT(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, fbo_width, fbo_height);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, main_fbo_depth);

    glBindFramebufferEXT(GL_FRAMEBUFFER, 0);
  }
}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void RenderTerm (void)
{

#ifdef WINDOWS
  if (!hRC)
    return;
  wglDeleteContext (hRC);
  hRC = NULL;
#elif defined(WAYLAND)
  // Wayland context handled externally
#else
  Display *dpy = WinGetDisplay();

  if (!ctx)
    return;

  glXMakeCurrent(dpy, None, NULL);
  glXDestroyContext(dpy, ctx);
  ctx = NULL;
#endif

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

#ifndef WINDOWS

// Build the bitmap-font display lists from the glyphs baked into font_data.c
// (see fontgen.c). No FreeType / fontconfig / .ttf files are touched at runtime;
// the display lists are identical to what the old FreeType path produced. dpy /
// vis are unused (kept for the shared X11/Wayland call signature).
static bool RenderLoadFonts(Display *dpy, Visual *vis)
{
  GLint  alignment;

  (void)dpy; (void)vis;

  if ((int)FONT_COUNT != font_count)
    std::cerr << "Warning: FONT_COUNT (" << FONT_COUNT << ") != baked font_count ("
              << font_count << "); regenerate font_data.c (make regen-fonts).\n";

  glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  for (unsigned int i = 0; i < FONT_COUNT; i++) {
    // glListBase in RenderPrint indexes lists by (char - font_first_char), so the
    // block must be contiguous and cover the same range fontgen baked (32..126).
    fonts[i].base_char = glGenLists(font_glyph_count);
    for (int j = 0; j < font_glyph_count; j++) {
      const Glyph& g = font_glyphs[i][j];
      glNewList(fonts[i].base_char + j, GL_COMPILE);
      glBitmap(g.width, g.height, g.xorig, g.yorig, g.xmove, g.ymove, g.bits);
      glEndList();
    }
  }

  glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
  return true;
}
#endif /* !WINDOWS */

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void RenderInit (void)
{

#ifdef WINDOWS
  HWND              hWnd;
	unsigned		      PixelFormat;
  HFONT	            font;		
	HFONT	            oldfont;

  hWnd = WinHwnd ();
  if (!(hDC = GetDC (hWnd))) 
		YOUFAIL ("Can't Create A GL Device Context.") ;
	if (!(PixelFormat = ChoosePixelFormat(hDC,&pfd)))
		YOUFAIL ("Can't Find A Suitable PixelFormat.") ;
  if(!SetPixelFormat(hDC,PixelFormat,&pfd))
		YOUFAIL ("Can't Set The PixelFormat.");
	if (!(hRC = wglCreateContext (hDC)))	
		YOUFAIL ("Can't Create A GL Rendering Context.");
  if(!wglMakeCurrent(hDC,hRC))	
		YOUFAIL ("Can't Activate The GL Rendering Context.");
  //Load the fonts for printing debug info to the window.
  for (int i = 0; i < FONT_COUNT; i++) {
	  fonts[i].base_char = glGenLists(96); 
	  font = CreateFont (FONT_SIZE,	0, 0,	0,	
				  FW_BOLD, FALSE,	FALSE, FALSE,	DEFAULT_CHARSET,	OUT_TT_PRECIS,		
				  CLIP_DEFAULT_PRECIS,	ANTIALIASED_QUALITY, FF_DONTCARE|DEFAULT_PITCH,
				  fonts[i].name);
	  oldfont = (HFONT)SelectObject(hDC, font);	
	  wglUseFontBitmaps(hDC, 32, 96, fonts[i].base_char);
	  SelectObject(hDC, oldfont);
	  DeleteObject(font);		
  }
#elif defined(WAYLAND)
  if(!RenderLoadFonts(NULL, NULL)) {
    std::cerr << "Couldn't load fonts...\n";
    return;
  }
#else
  Display      *dpy = WinGetDisplay();
  XVisualInfo  *vis = WinGetVisual();

  if(!vis) {
    std::cerr << "huh? no visual has been set...\n";
    return;
  }

  if(!(ctx = glXCreateContext(dpy, vis, NULL, True))) {
    std::cerr << "Could not create a GLX rendering context: " << strerror(errno) << ".\n";
    return;
  }

  glXMakeCurrent(dpy, WinGetWindow(), ctx);

  if(!RenderLoadFonts(dpy, vis->visual)) {
    std::cerr << "Couldn't load fonts...\n";
    return;
  }
#endif

  //If the program is running for the first time, set the defaults.
  if (!IniInt ("SetDefaults")) {
    IniIntSet ("SetDefaults", 1);
    IniIntSet ("Effect", EFFECT_BLOOM);
    IniIntSet ("ShowFog", 1);
  }
  //load in our settings
  letterbox = IniInt ("Letterbox") != 0;
  show_wireframe = IniInt ("Wireframe") != 0;
  show_fps = IniInt ("ShowFPS") != 0;
  show_fog = IniInt ("ShowFog") != 0;
  effect = IniInt ("Effect");
  flat = IniInt ("Flat") != 0;
  fog_distance = WORLD_HALF;
  //clear the viewport so the user isn't looking at trash while the program starts
  glViewport (0, 0, WinWidth (), WinHeight ());
  glClearColor (0.0f, 0.0f, 0.0f, 1.0f);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

#ifdef WINDOWS
  SwapBuffers (hDC);
#elif defined(WAYLAND)
  // Swapped externally
#else
  glFlush();
  glXSwapBuffers(dpy, WinGetWindow());
#endif

  LoadGLExtensions();
  RenderResize ();
}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void RenderFPSToggle ()
{

  show_fps = !show_fps;
  IniIntSet ("ShowFPS", show_fps ? 1 : 0);

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

bool RenderFog ()
{

  return show_fog;

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void RenderFogToggle ()
{

  show_fog = !show_fog;
  IniIntSet ("ShowFog", show_fog ? 1 : 0);

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void RenderLetterboxToggle ()
{

  letterbox = !letterbox;
  IniIntSet ("Letterbox", letterbox ? 1 : 0);
  RenderResize ();


}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void RenderScaleCycle ()
{

  // Cycle the internal 3D resolution. On fill-rate-bound GPUs a lower scale
  // buys frame time; the scene is bilinear-upscaled to the window on the final
  // blit, so the cost is a little softness. Watch the FPS counter (P) to tune.
  static const float steps[] = { 1.0f, 0.85f, 0.75f, 0.66f, 0.5f };
  const int          n = sizeof (steps) / sizeof (steps[0]);
  int                cur = 0, i;

  for (i = 0; i < n; i++)
    if (fabs (render_scale - steps[i]) < 0.01f) { cur = i; break; }
  render_scale = steps[(cur + 1) % n];
  IniIntSet ("RenderScale", (int)(render_scale * 100.0f + 0.5f));
  RenderResize ();

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void RenderWireframeToggle ()
{

  show_wireframe = !show_wireframe;
  IniIntSet ("Wireframe", show_wireframe ? 1 : 0);

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

bool RenderWireframe ()
{

  return show_wireframe;

}


/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void RenderEffectCycle ()
{

  effect = (effect + 1) % EFFECT_COUNT;
  IniIntSet ("Effect", effect);

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

bool RenderBloom ()
{

  return effect == EFFECT_BLOOM || effect == EFFECT_BLOOM_RADIAL 
    || effect == EFFECT_DEBUG_OVERBLOOM || effect == EFFECT_COLOR_CYCLE;

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

bool RenderFlat ()
{

  return flat;

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void RenderFlatToggle ()
{

  flat = !flat;
  IniIntSet ("Flat", flat ? 1 : 0);

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void RenderHelpToggle ()
{

  show_help = !show_help;

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

float RenderFogDistance ()
{

  return fog_distance;

}

/*-----------------------------------------------------------------------------

  This is used to set a gradient fog that goes from camera to some portion of 
  the normal fog distance.  This is used for making wireframe outlines and
  flat surfaces fade out after rebuild.  Looks cool.

-----------------------------------------------------------------------------*/

void RenderFogFX (float scalar)
{

  if (scalar >= 1.0f) {
    glDisable (GL_FOG);
    return;
  }
  glFogf (GL_FOG_START, 0.0f);
  glFogf (GL_FOG_END, fog_distance * 2.0f * scalar);
  glEnable (GL_FOG);

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void RenderUpdate (void)		
{

  GLvector        pos;
  GLvector        angle;
  GLrgba          color;
  int             elapsed;

  frames++;
  do_fps ();

  // During loading, draw the progress screen directly to the default
  // framebuffer and return immediately — don't touch FBO or scene state.
  if (LOADING_SCREEN && !EntityReady ()) {
    do_loading_screen ();
#ifdef WINDOWS
    SwapBuffers (hDC);
#elif defined(WAYLAND)
  // Swapped externally
#else
    glXSwapBuffers(WinGetDisplay(), WinGetWindow());
#endif
    return;
  }
  
  if (glBindFramebufferEXT && main_fbo) {
      glBindFramebufferEXT(GL_FRAMEBUFFER, main_fbo);
  }

  // The scene is drawn into the (possibly downscaled) FBO, so use FBO dims.
  glViewport (0, 0, fbo_width, fbo_height);
  glDepthMask (true);
  glClearColor (0.0f, 0.0f, 0.0f, 1.0f);
  glEnable(GL_DEPTH_TEST);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  if (letterbox)
    glViewport (0, (int)(letterbox_offset * render_scale), fbo_width, fbo_height);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
  glShadeModel(GL_SMOOTH);
  glFogi (GL_FOG_MODE, GL_LINEAR);
	glDepthFunc(GL_LEQUAL);
  glEnable (GL_CULL_FACE);
  glCullFace (GL_BACK);
  glEnable (GL_BLEND);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glMatrixMode (GL_TEXTURE);
  glLoadIdentity();
  glMatrixMode (GL_MODELVIEW);
  glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
  glHint(GL_FOG_HINT, GL_FASTEST);
  glLoadIdentity();
  glLineWidth (1.0f);
  pos = CameraPosition ();
  angle = CameraAngle ();
  glRotatef (angle.x, 1.0f, 0.0f, 0.0f);
  glRotatef (angle.y, 0.0f, 1.0f, 0.0f);
  glRotatef (angle.z, 0.0f, 0.0f, 1.0f);
  glTranslatef (-pos.x, -pos.y, -pos.z);
  glEnable (GL_TEXTURE_2D);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  //Render all the stuff in the whole entire world.
  glDisable (GL_FOG);
  SkyRender();
  if (show_fog) {
    glEnable (GL_FOG);
    glFogf (GL_FOG_START, fog_distance - 100);
    glFogf (GL_FOG_END, fog_distance);
    color = glRgba (0.0f);
    glFogfv (GL_FOG_COLOR, &color.red);
  }
  WorldRender ();
  if (effect == EFFECT_GLASS_CITY) {
    glDisable (GL_CULL_FACE);
    glEnable (GL_BLEND);
    glBlendFunc (GL_ONE, GL_ONE);
    glDepthFunc (false);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode (GL_TEXTURE);
    glTranslatef ((pos.x + pos.z) / SEGMENTS_PER_TEXTURE, 0, 0);
	  glMatrixMode (GL_MODELVIEW);
  } else {
    glEnable (GL_CULL_FACE);
    glDisable (GL_BLEND);
  }
  EntityRender ();
  if (!LOADING_SCREEN) {
    elapsed = 3000 - WorldSceneElapsed ();
    if (elapsed >= 0 && elapsed <= 3000) {
      // Transition effect removed to save 50% geometry draw time
    }
  } 
  if (EntityReady ())
    LightRender ();
  CarRender ();
  if (show_wireframe) {
    glDisable (GL_TEXTURE_2D);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    EntityRender ();
  }

  // Draw the post-processing quad
  if (glBindFramebufferEXT && main_fbo) {
      glBindFramebufferEXT(GL_FRAMEBUFFER, 0);
      glViewport (0, 0, WinWidth (), WinHeight ());
      glClearColor (0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      glMatrixMode (GL_PROJECTION);
      glPushMatrix();
      glLoadIdentity ();
      glOrtho (0, 1, 1, 0, -1, 1);
      glMatrixMode (GL_MODELVIEW);
      glPushMatrix();
      glLoadIdentity ();

      glDisable(GL_DEPTH_TEST);
      glDisable(GL_FOG);
      glDisable(GL_BLEND);
      glDisable(GL_CULL_FACE);
      glEnable(GL_TEXTURE_2D);

      if (post_shader && RenderBloom()) {
          post_shader->Bind();
          // Blur taps are in texels of the FBO texture, which is the scaled size.
          post_shader->SetUniform2f("resolution", (float)fbo_width, (float)fbo_height);
          post_shader->SetUniform1i("tex", 0);
      }

      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, main_fbo_tex);

      glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
      glBegin(GL_QUADS);
      glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 1.0f);
      glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 1.0f);
      glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 0.0f);
      glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 0.0f);
      glEnd();

      if (post_shader && RenderBloom()) {
          post_shader->Unbind();
      }

      glBindTexture(GL_TEXTURE_2D, 0);

      glMatrixMode (GL_PROJECTION);
      glPopMatrix();
      glMatrixMode (GL_MODELVIEW);
      glPopMatrix();
  }

  // Title-card cross-fade. WorldFade() is 1 while the scene is covered and ramps
  // to 0 as the city fades in (FADE_IN), so a black veil + the pixellated title,
  // both at that alpha, dissolve together to reveal the city underneath. This is
  // what turns the held title card into a cross-fade (and bookends every scene
  // rebuild). At fade 0 (steady state) nothing here draws.
  if (LOADING_SCREEN) {
    float f = WorldFade ();
    if (f > 0.0f) {
      glMatrixMode (GL_PROJECTION);
      glPushMatrix ();
      glLoadIdentity ();
      glOrtho (0, render_width, render_height, 0, 0.1f, 2048);
      glMatrixMode (GL_MODELVIEW);
      glPushMatrix ();
      glLoadIdentity ();
      glTranslatef (0, 0, -1.0f);
      glDisable (GL_DEPTH_TEST);
      glDepthMask (false);
      glDisable (GL_FOG);
      glDisable (GL_CULL_FACE);
      glDisable (GL_TEXTURE_2D);
      glEnable (GL_BLEND);
      glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glColor4f (0, 0, 0, f);
      glBegin (GL_QUADS);
      glVertex2i (0, 0);
      glVertex2i (0, render_height);
      glVertex2i (render_width, render_height);
      glVertex2i (render_width, 0);
      glEnd ();
      // Title alpha = min(cross-fade, fade-in ramp). During the hold (f==1) this
      // is the fade-in ramp, so a title still fading up when the scene becomes
      // ready keeps rising smoothly instead of popping to full; during the
      // cross-fade (f: 1 -> 0) the ramp is already 1, so it tracks f and fades out.
      float fin = loading_start_ms
                ? (float)(GetTimeInMillis () - loading_start_ms) / (float)TITLE_FADE_IN_MS
                : 1.0f;
      if (fin > 1.0f) fin = 1.0f;
      draw_pixel_title (f < fin ? f : fin);
      glDepthMask (true);
      glEnable (GL_DEPTH_TEST);
      glPopMatrix ();
      glMatrixMode (GL_PROJECTION);
      glPopMatrix ();
      glMatrixMode (GL_MODELVIEW);
    }
  }

  if (generate_icon) {
    glMatrixMode (GL_PROJECTION);
    glPushMatrix ();
    glLoadIdentity ();
    glOrtho (0, render_width, render_height, 0, 0.1f, 2048);
    glDisable(GL_DEPTH_TEST);
    glDepthMask (false);
    glMatrixMode (GL_MODELVIEW);
    glPushMatrix ();
    glLoadIdentity();
    glTranslatef(0, 0, -1.0f);				
    glDisable (GL_BLEND);
    glDisable (GL_FOG);
    glDisable (GL_TEXTURE_2D);

    int text_width = 126;
    int text_x = (render_width - text_width) / 2;
    int text_y = render_height - 60;

    RenderPrint (text_x + 2, text_y - 2, 6, glRgba (0.0f, 0.0f, 0.0f, 0.8f), "PixelCity");
    RenderPrint (text_x + 2, text_y + 2, 6, glRgba (0.0f, 0.0f, 0.0f, 0.8f), "PixelCity");
    RenderPrint (text_x, text_y, 6, glRgba (0.0f, 0.9f, 1.0f, 1.0f), "PixelCity");

    glPopMatrix ();
    glMatrixMode (GL_PROJECTION);
    glPopMatrix ();
    glMatrixMode (GL_MODELVIEW);
  }

  //Framerate tracker
  if (show_fps && !generate_icon) 
    RenderPrint (1, "FPS=%d : Scale=%d%% : Entities=%d : polys=%d", current_fps, (int)(render_scale * 100.0f + 0.5f), EntityCount () + LightCount () + CarCount (), EntityPolyCount () + LightCount () + CarCount ());
  //Show the help overlay
  if (show_help && !generate_icon)
    do_help ();

#ifdef WINDOWS
  SwapBuffers (hDC);
#elif defined(WAYLAND)
  // Swapped externally
#else
  glXSwapBuffers(WinGetDisplay(), WinGetWindow());
#endif

}
