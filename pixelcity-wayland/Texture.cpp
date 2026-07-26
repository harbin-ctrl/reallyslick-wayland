/*-----------------------------------------------------------------------------

  Texture.cpp

  2009 Shamus Young

-------------------------------------------------------------------------------
  
  This procedurally builds all of the textures.  

  I apologize in advance for the apalling state of this module. It's the victim 
  of iterative and experimental development.  It has cruft, poorly named
  functions, obscure code, poorly named variables, and is badly organized. Even
  the formatting sucks in places. Its only saving grace is that it works.
  
-----------------------------------------------------------------------------*/

#define RANDOM_COLOR_SHIFT  ((float)(RandomVal (10)) / 50.0f)
#define RANDOM_COLOR_VAL    ((float)(RandomVal (256)) / 256.0f)
#define RANDOM_COLOR_LIGHT  ((float)(200 + RandomVal (56)) / 256.0f)
#define SKY_BANDS           (sizeof (sky_pos) / sizeof (int))
#define PREFIX_COUNT        (sizeof (prefix) / sizeof (char*))
#define SUFFIX_COUNT        (sizeof (suffix) / sizeof (char*))
#define NAME_COUNT          (sizeof (name) / sizeof (char*))

#ifdef WINDOWS
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <thread>
#include <atomic>
#include <vector>
#define GL_GLEXT_PROTOTYPES   // expose glGenerateMipmap (core GL 3.0)
#include <GL/gl.h>
#include <GL/glu.h>

#if defined(WINDOWS) && _MSC_VER <= 1200
#include <GL/glaux.h>
#endif

#include "glTypes.h"
#include "Building.h"
#include "Camera.h"
#include "Car.h"
#include "Light.h"
#include "Macro.h"
#include "Random.h"
#include "Render.h"
#include "Sky.h"
#include "Texture.h"
#include "World.h"
#include "Win.h"
#include "time_util.h"

static const char*        prefix[] =
{
  "i",
  "Green ",
  "Mega",
  "Super ",
  "Omni",
  "e",
  "Hyper",
  "Global ",
  "Vital",
  "Next ",
  "Pacific ",
  "Metro",
  "Unity ",
  "G-",
  "Trans",
  "Infinity ",
  "Superior ",
  "Monolith ",
  "Best ",
  "Atlantic ",
  "First ",
  "Union ",
  "National ",
  "Cyber",
  "Neo",
  "Quantum ",
  "Apex ",
  "Vertex ",
  "Prime ",
  "Ultra",
  "Astro",
  "Techno",
  "Novo",
  "Euro ",
  "Sino ",
  "Vanguard ",
  "Sterling ",
  "Summit ",
  "Zenith ",
  "Orbital ",
  "Continental ",
  "Federated ",
  "Allied ",
  "Standard ",
  "General ",
  "Consolidated ",
  "Dynamic ",
  "Advanced ",
  "Sovereign ",
  "Paramount ",
  "Fusion ",
  "Titan ",
  "Meridian ",
  "Frontier ",
  "Imperial ",
  "Colonial ",
  "Peak ",
};

static const char*        name[] = 
{
  "Biotic",
  "Info",
  "Data",
  "Solar",
  "Aerospace",
  "Motors",
  "Nano",
  "Online",
  "Circuits",
  "Energy",
  "Med",
  "Robotic",
  "Exports",
  "Security",
  "Systems",
  "Financial",
  "Industrial",
  "Media",
  "Materials",
  "Foods",
  "Networks",
  "Shipping",
  "Tools",
  "Medical",
  "Publishing",
  "Enterprises",
  "Audio",
  "Health",
  "Bank",
  "Imports",
  "Apparel",
  "Petroleum",
  "Studios",
  "Dynamics",
  "Genetics",
  "Logistics",
  "Chemical",
  "Plastics",
  "Textiles",
  "Pharma",
  "Cybernetics",
  "Automation",
  "Diagnostics",
  "Analytics",
  "Optics",
  "Telecom",
  "Freight",
  "Refining",
  "Mining",
  "Agriculture",
  "Retail",
  "Ventures",
  "Holdings",
  "Capital",
  "Insurance",
  "Realty",
  "Engineering",
  "Consulting",
  "Software",
  "Semiconductors",
  "Reactors",
  "Turbines",
  "Aviation",
  "Transit",
  "Utilities",
  "Power",
  "Synthetics",
  "Alloys",
  "Ceramics",
  "Instruments",
  "Devices",
  "Cosmetics",
  "Genomics",
  "Cloud",
  "Digital",
};

static const char*        suffix[] = 
{
  "Corp",
  " Inc.",
  "Co",
  "World",
  ".Com",
  " USA",
  " Ltd.",
  "Net",
  " Tech",
  " Labs",
  " Mfg.",
  " UK",
  " Unlimited",
  " One",
  " LLC",
  " Group",
  " Holdings",
  " International",
  " Worldwide",
  " Industries",
  " Solutions",
  " Partners",
  " Associates",
  " GmbH",
  " S.A.",
  " AG",
  " PLC",
  " & Co.",
  " Consortium",
  " Syndicate",
  ".Net",
  " Global",
  " Direct",
  " Systems",
  " Trust",
};
  
class CTexture
{
public:
  int               _my_id;
  unsigned          _glid;
  int               _desired_size;
  int               _size;
  int               _half;
  int               _segment_size;
  bool              _ready;
  bool              _masked;
  bool              _mipmap;
  bool              _clamp;
  uint32_t          _seed;      // per-texture RNG seed for the software window bake
public:
  CTexture*         _next;
                    CTexture (int id, int size, bool mipmap, bool clamp, bool masked);
  void              Clear () { _ready = false; }
  void              Rebuild ();
  bool              RebuildSoftwareWindows ();
  void              DrawWindows ();
  void              DrawSky ();
  void              DrawHeadlight ();
};

static CTexture*    head;
static bool         textures_done;
static bool         prefix_used[PREFIX_COUNT];
static bool         name_used[NAME_COUNT];
static bool         suffix_used[SUFFIX_COUNT];
static int          build_time;

/*-----------------------------------------------------------------------------
                          
-----------------------------------------------------------------------------*/

void drawrect_simple (int left, int top, int right, int bottom, GLrgba color)
{

  glColor3fv (&color.red);
  glBegin (GL_QUADS);
  glVertex2i (left, top);
  glVertex2i (right, top);
  glVertex2i (right, bottom);
  glVertex2i (left, bottom);
  glEnd ();

}


/*-----------------------------------------------------------------------------
                          
-----------------------------------------------------------------------------*/

void drawrect_simple (int left, int top, int right, int bottom, GLrgba color1, GLrgba color2)
{

  glColor3fv (&color1.red);
  glBegin (GL_TRIANGLE_FAN);
  glVertex2i ((left + right) / 2, (top + bottom) / 2);
  glColor3fv (&color2.red);
  glVertex2i (left, top);
  glVertex2i (right, top);
  glVertex2i (right, bottom);
  glVertex2i (left, bottom);
  glVertex2i (left, top);
  glEnd ();

}


/*-----------------------------------------------------------------------------
                          
-----------------------------------------------------------------------------*/

void drawrect (int left, int top, int right, int bottom, GLrgba color)
{

  float     average;
  float     hue;
  int       potential;
  int       height;
  int       i, j;
  bool      bright;
  GLrgba    color_noise;

  // GL state (cull, blend, line width, polygon mode) is now set once in
  // DrawWindows() rather than redundantly on every call.
  glColor3fv (&color.red);

  if (left == right) { //in low resolution, a "rect" might be 1 pixel wide
    glBegin (GL_LINES);
    glVertex2i (left, top);
    glVertex2i (left, bottom);
    glEnd ();
  } if (top == bottom) { //in low resolution, a "rect" might be 1 pixel wide
    glBegin (GL_LINES);
    glVertex2i (left, top);
    glVertex2i (right, top);
    glEnd ();
  } else { // draw one of those fancy 2-dimensional rectangles
    glBegin (GL_QUADS);
    glVertex2i (left, top);
    glVertex2i (right, top);
    glVertex2i (right, bottom);
    glVertex2i (left, bottom);
    glEnd ();


    average = (color.red + color.blue + color.green) / 3.0f;
    bright = average > 0.5f;
    potential = (int)(average * 255.0f);

    if (bright) {
      glBegin (GL_POINTS);
      for (i = left + 1; i < right - 1; i++) {
        for (j = top + 1; j < bottom - 1; j++) {
          hue = 0.2f + (float)RandomVal (100) / 300.0f + (float)RandomVal (100) / 300.0f + (float)RandomVal (100) / 300.0f;
          color_noise = glRgbaFromHsl (hue, 0.3f, 0.5f);
          color_noise.alpha = (float)RandomVal (potential) / 144.0f;
          glColor4fv (&color_noise.red);
          glVertex2i (i, j);
        }
      }
      glEnd ();
    }
    // Vertical noise lines across the window — batched into a single
    // GL_LINES block instead of one glBegin/glEnd per column.
    height = (bottom - top) + (RandomVal (3) - 1) + (RandomVal(3) - 1);
    glBegin (GL_LINES);
    for (i = left; i < right; i++) {
      if (RandomVal (3) == 0)
        RandomVal (4);  // consume the RNG value for compatibility
      if (RandomVal (6) == 0) {
        height = bottom - top;
        height = RandomVal (height);
        height = RandomVal (height);
        height = RandomVal (height);
        height = ((bottom - top) + height) / 2;
      }
      glColor4f (0, 0, 0, (float)RandomVal (256) / 256.0f);
      glVertex2i (i, bottom - height);
      glColor4f (0, 0, 0, (float)RandomVal (256) / 256.0f);
      glVertex2i (i, bottom);
    }
    glEnd ();
  }
}

/*-----------------------------------------------------------------------------
                          
-----------------------------------------------------------------------------*/

static void window (int x, int y, int size, int id, GLrgba color)
{

  int     margin;
  int     half;
  int     i;

  margin = size / 3;
  half = size / 2;
  switch (id) {
  case TEXTURE_BUILDING1: //filled, 1-pixel frame
    drawrect (x + 1, y + 1, x + size - 1, y + size - 1, color);
    break;
  case TEXTURE_BUILDING2: //vertical
    drawrect (x + margin, y + 1, x + size - margin, y + size - 1, color);
    break;
  case TEXTURE_BUILDING3: //side-by-side pair
    drawrect (x + 1, y + 1, x + half - 1, y + size - margin, color);
    drawrect (x + half + 1, y + 1, x + size - 1, y + size - margin,  color);
    break;
  case TEXTURE_BUILDING4: //windows with blinds
    drawrect (x + 1, y + 1, x + size - 1, y + size - 1, color);
    i = RandomVal (size - 2);
    drawrect (x + 1, y + 1, x + size - 1, y + i + 1, color * 0.3f);

    break;
  case TEXTURE_BUILDING5: //vert stripes
    drawrect (x + 1, y + 1, x + size - 1, y + size - 1, color);
    drawrect (x + margin, y + 1, x + margin, y + size - 1, color * 0.7f);
    drawrect (x + size - margin - 1, y + 1, x + size - margin - 1, y + size - 1, color * 0.3f);
    break;
  case TEXTURE_BUILDING6: //wide horz line
    drawrect (x + 1, y + 1, x + size - 1, y + size - margin, color);
    break;
  case TEXTURE_BUILDING7: //4-pane
    drawrect (x + 2, y + 1, x + size - 1, y + size - 1, color);
    drawrect (x + 2, y + half, x + size - 1, y + half, color * 0.2f);
    drawrect (x + half, y + 1, x + half, y + size - 1, color * 0.2f);
    break;
  case TEXTURE_BUILDING8: // Single narrow window
    drawrect (x + half - 1, y + 1, x + half + 1, y + size - margin, color);
    break;
  case TEXTURE_BUILDING9: //horizontal
    drawrect (x + 1, y + margin, x + size - 1, y + size - margin - 1, color);
    break;
  }

}

/*-----------------------------------------------------------------------------

  Software rasteriser for the building-window textures.

  A faithful CPU port of DrawWindows()/window()/drawrect() above, writing RGBA8
  into a caller-supplied buffer instead of issuing immediate-mode GL. This lets
  the nine building textures (~1s of serial GL draw at startup) be generated on
  worker threads with no GL context, then uploaded on the main thread. Same
  algorithm and RNG-call sequence -> same look; only the RNG source differs (a
  per-texture seeded stream instead of the shared global one).

-----------------------------------------------------------------------------*/

// Self-contained xorshift32 PRNG -- one instance per texture, so no shared state
// and no locking. Quality/range are ample for window noise.
struct SoftRng
{
  uint32_t s;
  SoftRng (uint32_t seed) : s (seed ? seed : 0x1234567u) {}
  uint32_t next () { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  int      val (int range) { return range ? (int)(next () % (uint32_t)range) : 0; }
};

static inline void soft_put (uint8_t* buf, int size, int x, int y,
                             float r, float g, float b)
{
  if (x < 0 || y < 0 || x >= size || y >= size)
    return;
  uint8_t* p = buf + ((size_t)y * size + x) * 4;
  p[0] = (uint8_t)(r <= 0 ? 0 : (r >= 1 ? 255 : r * 255.0f + 0.5f));
  p[1] = (uint8_t)(g <= 0 ? 0 : (g >= 1 ? 255 : g * 255.0f + 0.5f));
  p[2] = (uint8_t)(b <= 0 ? 0 : (b >= 1 ? 255 : b * 255.0f + 0.5f));
  p[3] = 255;
}

static inline void soft_blend (uint8_t* buf, int size, int x, int y,
                               float r, float g, float b, float a)
{
  if (x < 0 || y < 0 || x >= size || y >= size || a <= 0.0f)
    return;
  if (a > 1.0f) a = 1.0f;
  uint8_t* p = buf + ((size_t)y * size + x) * 4;
  p[0] = (uint8_t)(p[0] * (1.0f - a) + r * 255.0f * a);
  p[1] = (uint8_t)(p[1] * (1.0f - a) + g * 255.0f * a);
  p[2] = (uint8_t)(p[2] * (1.0f - a) + b * 255.0f * a);
}

// CPU port of drawrect(): the base fill is opaque; bright windows get colour
// sparkle points; every window gets dark vertical noise streaks (Gouraud alpha).
static void soft_drawrect (uint8_t* buf, int size, int left, int top, int right, int bottom,
                           float cr, float cg, float cb, SoftRng& rng)
{
  int i, j;
  if (left == right) {                        // 1px vertical line (opaque)
    for (j = top; j < bottom; j++)
      soft_put (buf, size, left, j, cr, cg, cb);
  }
  if (top == bottom) {                        // 1px horizontal line (opaque)
    for (i = left; i < right; i++)
      soft_put (buf, size, i, top, cr, cg, cb);
  } else {                                    // filled rect + noise
    for (j = top; j < bottom; j++)
      for (i = left; i < right; i++)
        soft_put (buf, size, i, j, cr, cg, cb);

    float average = (cr + cb + cg) / 3.0f;
    bool  bright = average > 0.5f;
    int   potential = (int)(average * 255.0f);

    if (bright) {
      for (i = left + 1; i < right - 1; i++) {
        for (j = top + 1; j < bottom - 1; j++) {
          float hue = 0.2f + (float)rng.val (100) / 300.0f
                            + (float)rng.val (100) / 300.0f
                            + (float)rng.val (100) / 300.0f;
          GLrgba cn = glRgbaFromHsl (hue, 0.3f, 0.5f);
          float a = (float)rng.val (potential) / 144.0f;
          soft_blend (buf, size, i, j, cn.red, cn.green, cn.blue, a);
        }
      }
    }
    int height = (bottom - top) + (rng.val (3) - 1) + (rng.val (3) - 1);
    for (i = left; i < right; i++) {
      if (rng.val (3) == 0)
        rng.val (4);                          // consume, matches the GL path
      if (rng.val (6) == 0) {
        int h = bottom - top;
        h = rng.val (h);
        h = rng.val (h);
        h = rng.val (h);
        height = ((bottom - top) + h) / 2;
      }
      float a_top = (float)rng.val (256) / 256.0f;
      float a_bot = (float)rng.val (256) / 256.0f;
      int y0 = bottom - height;
      for (j = y0; j < bottom; j++) {
        float t = (height > 0) ? (float)(j - y0) / (float)height : 0.0f;
        soft_blend (buf, size, i, j, 0, 0, 0, a_top * (1.0f - t) + a_bot * t);
      }
    }
  }
}

// CPU port of window(): the nine building styles, scaling the base colour.
static void soft_window (uint8_t* buf, int size, int x, int y, int seg, int id,
                         float cr, float cg, float cb, SoftRng& rng)
{
  int margin = seg / 3;
  int half = seg / 2;
  int i;
  switch (id) {
  case TEXTURE_BUILDING1:
    soft_drawrect (buf, size, x+1, y+1, x+seg-1, y+seg-1, cr, cg, cb, rng);
    break;
  case TEXTURE_BUILDING2:
    soft_drawrect (buf, size, x+margin, y+1, x+seg-margin, y+seg-1, cr, cg, cb, rng);
    break;
  case TEXTURE_BUILDING3:
    soft_drawrect (buf, size, x+1, y+1, x+half-1, y+seg-margin, cr, cg, cb, rng);
    soft_drawrect (buf, size, x+half+1, y+1, x+seg-1, y+seg-margin, cr, cg, cb, rng);
    break;
  case TEXTURE_BUILDING4:
    soft_drawrect (buf, size, x+1, y+1, x+seg-1, y+seg-1, cr, cg, cb, rng);
    i = rng.val (seg-2);
    soft_drawrect (buf, size, x+1, y+1, x+seg-1, y+i+1, cr*0.3f, cg*0.3f, cb*0.3f, rng);
    break;
  case TEXTURE_BUILDING5:
    soft_drawrect (buf, size, x+1, y+1, x+seg-1, y+seg-1, cr, cg, cb, rng);
    soft_drawrect (buf, size, x+margin, y+1, x+margin, y+seg-1, cr*0.7f, cg*0.7f, cb*0.7f, rng);
    soft_drawrect (buf, size, x+seg-margin-1, y+1, x+seg-margin-1, y+seg-1, cr*0.3f, cg*0.3f, cb*0.3f, rng);
    break;
  case TEXTURE_BUILDING6:
    soft_drawrect (buf, size, x+1, y+1, x+seg-1, y+seg-margin, cr, cg, cb, rng);
    break;
  case TEXTURE_BUILDING7:
    soft_drawrect (buf, size, x+2, y+1, x+seg-1, y+seg-1, cr, cg, cb, rng);
    soft_drawrect (buf, size, x+2, y+half, x+seg-1, y+half, cr*0.2f, cg*0.2f, cb*0.2f, rng);
    soft_drawrect (buf, size, x+half, y+1, x+half, y+seg-1, cr*0.2f, cg*0.2f, cb*0.2f, rng);
    break;
  case TEXTURE_BUILDING8:
    soft_drawrect (buf, size, x+half-1, y+1, x+half+1, y+seg-margin, cr, cg, cb, rng);
    break;
  case TEXTURE_BUILDING9:
    soft_drawrect (buf, size, x+1, y+margin, x+seg-1, y+seg-margin-1, cr, cg, cb, rng);
    break;
  }
}

// CPU port of DrawWindows(): fills one building texture into buf (RGBA8, size x
// size). Pure CPU + caller's seed -> safe to run on a worker thread.
static void soft_draw_windows (uint8_t* buf, int size, int id, uint32_t seed)
{
  SoftRng rng (seed);
  int seg = size / SEGMENTS_PER_TEXTURE;
  int run = 0;
  int run_length = rng.val (9) + 2;
  int lit_density = 2 + rng.val (2) + rng.val (2);
  bool lit = false;

  for (int y = 0; y < SEGMENTS_PER_TEXTURE; y++) {
    if (!(y % 8) && y > 0) {
      run = 0;
      run_length = rng.val (9) + 2;
      lit_density = 2 + rng.val (2) + rng.val (2);
      lit = false;
    }
    for (int x = 0; x < SEGMENTS_PER_TEXTURE; x++) {
      if (run < 1) {
        run = rng.val (run_length);
        lit = rng.val (lit_density) == 0;
      }
      float cr, cg, cb;
      if (lit) {
        float v = 0.5f + (float)(rng.next () % 128) / 256.0f;
        cr = v + (float)rng.val (10) / 50.0f;   // per-channel tint (RANDOM_COLOR_SHIFT)
        cg = v + (float)rng.val (10) / 50.0f;
        cb = v + (float)rng.val (10) / 50.0f;
      } else {
        cr = cg = cb = (float)(rng.next () % 40) / 256.0f;
      }
      soft_window (buf, size, x * seg, y * seg, seg, id, cr, cg, cb, rng);
      run--;
    }
  }
}

// Bake a building-window texture on the CPU and upload it. Returns false for
// non-building textures (leaving them on the original GL immediate-mode path).
bool CTexture::RebuildSoftwareWindows ()
{
  if (_my_id < TEXTURE_BUILDING1 || _my_id > TEXTURE_BUILDING9)
    return false;

  _size = _desired_size;                       // 512, always within GL limits
  uint8_t* buf = (uint8_t*) calloc ((size_t)_size * _size * 4, 1);
  if (!buf)
    return false;
  soft_draw_windows (buf, _size, _my_id, _seed);

  glBindTexture (GL_TEXTURE_2D, _glid);
  glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, _size, _size, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
  free (buf);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glGenerateMipmap (GL_TEXTURE_2D);
  _ready = true;
  return true;
}

/*-----------------------------------------------------------------------------

  Parallel bake of the building-window textures.

  soft_draw_windows() is pure CPU, so the nine building textures can be filled on
  a worker pool (lock-free work-stealing via an atomic index) and then uploaded
  on the main/GL thread. This is the foundation Arc 2 (the streaming "city wakes
  up" reveal) builds on -- generation lives off the GL thread.

-----------------------------------------------------------------------------*/

struct BakeUnit { uint8_t* buf; int size; int id; uint32_t seed; };

static void bake_worker (BakeUnit* units, int count, std::atomic<int>* next)
{
  for (;;) {
    int i = next->fetch_add (1);
    if (i >= count)
      break;
    if (units[i].buf)
      soft_draw_windows (units[i].buf, units[i].size, units[i].id, units[i].seed);
  }
}

static void bake_building_textures_parallel ()
{
  std::vector<CTexture*> texs;
  for (CTexture* t = head; t; t = t->_next)
    if (t->_my_id >= TEXTURE_BUILDING1 && t->_my_id <= TEXTURE_BUILDING9 && !t->_ready)
      texs.push_back (t);
  if (texs.empty ())
    return;

  std::vector<BakeUnit> units (texs.size ());
  for (size_t i = 0; i < texs.size (); i++) {
    texs[i]->_size = texs[i]->_desired_size;     // 512, within GL limits
    int sz = texs[i]->_size;
    units[i].buf  = (uint8_t*) calloc ((size_t)sz * sz * 4, 1);
    units[i].size = sz;
    units[i].id   = texs[i]->_my_id;
    units[i].seed = texs[i]->_seed;
  }

  // Fill on a worker pool; the main thread pitches in since it would otherwise
  // just block here. hardware_concurrency workers total keeps all cores busy.
  std::atomic<int> next (0);
  unsigned hw = std::thread::hardware_concurrency ();
  int nthreads = hw ? (int)hw : 2;
  if (nthreads > (int)units.size ())
    nthreads = (int)units.size ();
  std::vector<std::thread> pool;
  for (int t = 1; t < nthreads; t++)
    pool.emplace_back (bake_worker, units.data (), (int)units.size (), &next);
  bake_worker (units.data (), (int)units.size (), &next);
  for (auto& th : pool)
    th.join ();

  // Upload on this (GL) thread.
  for (size_t i = 0; i < texs.size (); i++) {
    if (!units[i].buf)
      continue;
    glBindTexture (GL_TEXTURE_2D, texs[i]->_glid);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, units[i].size, units[i].size, 0,
                  GL_RGBA, GL_UNSIGNED_BYTE, units[i].buf);
    free (units[i].buf);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap (GL_TEXTURE_2D);
    texs[i]->_ready = true;
  }
}

/*-----------------------------------------------------------------------------
                          
-----------------------------------------------------------------------------*/

CTexture::CTexture (int id, int size, bool mipmap, bool clamp, bool masked)
{

  glGenTextures (1, &_glid); 
  _my_id = id;
  _mipmap = mipmap;
  _clamp = clamp;
  _masked = masked;
  _desired_size = size;
  _size = size;
  _half = size / 2;
  _segment_size = size / SEGMENTS_PER_TEXTURE;
  _ready = false;
  // Independent seed per texture so the software window bake (RebuildSoftwareWindows)
  // is deterministic and thread-safe without touching the global RNG at bake time.
  _seed = (RandomVal () << 1) | 1u;
  _next = head;
  head = this;

}

/*-----------------------------------------------------------------------------

  This draws all of the windows on a building texture. lit_density controls 
  how many lights are on. (1 in n chance that the light is on. Higher values 
  mean less lit windows. run_length controls how often it will consider 
  changing the lit / unlit status. 1 produces a complete scatter, higher
  numbers make long strings of lights.
  
-----------------------------------------------------------------------------*/

void CTexture::DrawWindows ()
{


  int         x, y;
  int         run = 0;
  int         run_length = RandomVal(9) + 2;
  int         lit_density = 2 + RandomVal(2) + RandomVal(2);
  GLrgba      color;
  bool        lit = false;

  // Set GL state once for all windows (drawrect no longer sets it per call).
  glDisable (GL_CULL_FACE);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable (GL_BLEND);
  glLineWidth (1.0f);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  //color = glRgbaUnique (_my_id);
  for (y = 0; y < SEGMENTS_PER_TEXTURE; y++)  {
    //Every few floors we change the behavior
    if (!(y % 8) && y>0) {
      run = 0;
      run_length = RandomVal (9) + 2;
      lit_density = 2 + RandomVal(2) + RandomVal(2);
      lit = false;
    }
    for (x = 0; x < SEGMENTS_PER_TEXTURE; x++) {
      //if this run is over reroll lit and start a new one
      if (run < 1) {
        run = RandomVal (run_length);
        lit = RandomVal (lit_density) == 0;
        //if (lit)
          //color = glRgba (0.5f + (float)(RandomVal () % 128) / 256.0f) + glRgba (RANDOM_COLOR_SHIFT, RANDOM_COLOR_SHIFT, RANDOM_COLOR_SHIFT);
      }
      if (lit) 
        color = glRgba (0.5f + (float)(RandomVal () % 128) / 256.0f) + glRgba (RANDOM_COLOR_SHIFT, RANDOM_COLOR_SHIFT, RANDOM_COLOR_SHIFT);
       else 
        color = glRgba ((float)(RandomVal () % 40) / 256.0f);
      window (x * _segment_size, y * _segment_size, _segment_size, _my_id, color);
      run--;

    }
  }

}


/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void CTexture::DrawSky ()
{

  GLrgba          color;
  float           grey;
  float           scale, inv_scale;
  int             i, x, y;
  int             width, height;
  int             offset;
  int             width_adjust;
  int             height_adjust;

  color = WorldBloomColor ();
  grey = (color.red + color.green + color.blue) / 3.0f;
  //desaturate, slightly dim
  color = (color + glRgba (grey) * 2.0f) / 15.0f;
  glDisable (GL_BLEND);
  glBegin (GL_QUAD_STRIP);
  glColor3f (0,0,0);
  glVertex2i (0, _half);
  glVertex2i (_size, _half);
  glColor3fv (&color.red);
  glVertex2i (0, _size - 2);  
  glVertex2i (_size, _size - 2);  
  glEnd ();
  //Draw a bunch of little faux-buildings on the horizon.
  for (i = 0; i < _size; i += 5) 
    drawrect (i, _size - RandomVal (8) - RandomVal (8) - RandomVal (8), i + RandomVal (9), _size, glRgba (0.0f));
  //Draw the clouds
  for (i = _size - 30; i > 5; i -= 2) {

    x = RandomVal (_size);
    y = i;

    scale = 1.0f - ((float)y / (float)_size);
    width = RandomVal (_half / 2) + (int)((float)_half * scale) / 2;
    scale = 1.0f - (float)y / (float)_size;
    height = (int)((float)(width) * scale);
    height = MAX (height, 4);

    glEnable (GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable (GL_CULL_FACE);
    glEnable (GL_TEXTURE_2D);
    glBindTexture (GL_TEXTURE_2D, TextureId (TEXTURE_SOFT_CIRCLE));
    glDepthMask (false);
    glBegin (GL_QUADS);
    for (offset = -_size; offset <= _size; offset += _size) {
      for (scale = 1.0f; scale > 0.0f; scale -= 0.25f) {

        inv_scale = 1.0f - (scale);
        if (scale < 0.4f)
          color = WorldBloomColor () * 0.1f;
        else
          color = glRgba (0.0f);
        color.alpha = 0.2f;
        glColor4fv (&color.red);
        width_adjust = (int)((float)width / 2.0f + (int)(inv_scale * ((float)width / 2.0f)));
        height_adjust = height + (int)(scale * (float)height * 0.99f);
        glTexCoord2f (0, 0);   glVertex2i (offset + x - width_adjust, y + height - height_adjust);
        glTexCoord2f (0, 1);   glVertex2i (offset + x - width_adjust, y + height);
        glTexCoord2f (1, 1);   glVertex2i (offset + x + width_adjust, y + height);
        glTexCoord2f (1, 0);   glVertex2i (offset + x + width_adjust, y + height - height_adjust);
      }

    }
  }
  glEnd ();

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void CTexture::DrawHeadlight ()
{

  float           radius;
  int             i, x, y;
  GLvector2       pos;

  //Make a simple circle of light, bright in the center and fading out
  radius = ((float)_half) - 20;
  x = _half - 20;
  y = _half;
  glEnable (GL_BLEND);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBegin (GL_TRIANGLE_FAN);
  glColor4f (0.8f, 0.8f, 0.8f, 0.6f);
  glVertex2i (_half - 5, y);
  glColor4f (0, 0, 0, 0);
  for (i = 0; i <= 360; i += 36) {
    pos.x = sinf ((float)(i % 360) * DEGREES_TO_RADIANS) * radius;
    pos.y = cosf ((float)(i % 360) * DEGREES_TO_RADIANS) * radius;
    glVertex2i (x + (int)pos.x, _half + (int)pos.y);
  }
  glEnd ();
  x = _half + 20;
  glBegin (GL_TRIANGLE_FAN);
  glColor4f (0.8f, 0.8f, 0.8f, 0.6f);
  glVertex2i (_half + 5, y);
  glColor4f (0, 0, 0, 0);
  for (i = 0; i <= 360; i += 36) {
    pos.x = sinf ((float)(i % 360) * DEGREES_TO_RADIANS) * radius;
    pos.y = cosf ((float)(i % 360) * DEGREES_TO_RADIANS) * radius;
    glVertex2i (x + (int)pos.x, _half + (int)pos.y);
  }
  glEnd ();
  x = _half - 6;
  drawrect_simple (x - 3, y - 2, x + 2, y + 2, glRgba (1.0f));
  x = _half + 6;
  drawrect_simple (x - 2, y - 2, x + 3, y + 2, glRgba (1.0f));

}

/*-----------------------------------------------------------------------------

  Here is where ALL of the procedural textures are created.  It's filled with 
  obscure logic, magic numbers, and messy code. Part of this is because 
  there is a lot of "art" being done here, and lots of numbers that could be 
  endlessly tweaked.  Also because I'm lazy.
                    
-----------------------------------------------------------------------------*/

// Off-screen target used to bake textures. Rendering into a texture-sized FBO
// (instead of the window) decouples bake resolution from the window size, so a
// small startup window no longer caps logo/sky/building texture quality -- the
// city looks right the moment the user switches to fullscreen. Returns false if
// FBO objects are unavailable, in which case Rebuild falls back to the window.
// Uses core FBO entry points directly (this file defines GL_GLEXT_PROTOTYPES,
// as it already does for glGenerateMipmap) rather than the EXT pointers.
static GLuint bake_fbo = 0, bake_color = 0, bake_depth = 0;
static int    bake_size = 0;

static bool TextureBakeFboBind (int size)
{
  if (!bake_fbo) {
    glGenFramebuffers (1, &bake_fbo);
    glGenRenderbuffers (1, &bake_color);
    glGenRenderbuffers (1, &bake_depth);
  }
  glBindFramebuffer (GL_FRAMEBUFFER, bake_fbo);
  if (size != bake_size) {
    glBindRenderbuffer (GL_RENDERBUFFER, bake_color);
    glRenderbufferStorage (GL_RENDERBUFFER, GL_RGBA8, size, size);
    glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_RENDERBUFFER, bake_color);
    glBindRenderbuffer (GL_RENDERBUFFER, bake_depth);
    glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
    glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_RENDERBUFFER, bake_depth);
    bake_size = size;
  }
  if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    glBindFramebuffer (GL_FRAMEBUFFER, 0);
    return false;
  }
  return true;
}

void CTexture::Rebuild ()
{

  int             i, j;
  int             x, y;
  int             name_num, prefix_num, suffix_num;
  int             max_size;
  float           radius;
  GLvector2       pos;
  bool            use_framebuffer;
  bool            bake_to_fbo;
  unsigned        start;
  int             lapsed;

  start = GetTimeInMillis ();
  // Building-window textures bake on the CPU (see RebuildSoftwareWindows) instead
  // of via immediate-mode GL -- much faster and, shortly, parallelisable.
  if (RebuildSoftwareWindows ()) {
    build_time += GetTimeInMillis () - start;
    return;
  }
  _size = _desired_size;
  //Normally we bake by drawing into the window viewport, so a texture can't be
  //bigger than the view. Only when the desired size exceeds that (e.g. the 1024
  //logo atlas while the window is a small 800x600 at startup) do we bake into an
  //off-screen FBO instead -- limited only by GL_MAX_TEXTURE_SIZE -- so a small
  //startup window no longer permanently caps that texture's resolution. This
  //keeps every window-sized texture on the original, proven bake path.
  max_size = RenderMaxTextureSize ();
  bake_to_fbo = false;
  if (_size > max_size) {
    int gl_max = 0;
    glGetIntegerv (GL_MAX_TEXTURE_SIZE, &gl_max);
    int want = _desired_size;
    while (gl_max > 0 && want > gl_max)
      want /= 2;
    if (TextureBakeFboBind (want)) {
      _size = want;
      bake_to_fbo = true;
    }
  }
  if (!bake_to_fbo) {
    while (_size > max_size)
      _size /= 2;
  }
  glBindTexture(GL_TEXTURE_2D, _glid);
  //Set up the texture
  glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, _size, _size, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  if (_clamp) {
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  //Set up our viewport so that drawing into our texture will be as easy 
  //as possible.  We make the viewport and projection simply match the given 
  //texture size. 
  glViewport(0, 0, _size , _size);
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  glOrtho (0, _size, _size, 0, 0.1f, 2048);
	glMatrixMode (GL_MODELVIEW);
  glPushMatrix ();
  glLoadIdentity();
  glDisable (GL_CULL_FACE);
  glDisable (GL_FOG);
  glBindTexture(GL_TEXTURE_2D, 0);
  glTranslatef(0, 0, -10.0f);
  glClearColor (0, 0, 0, _masked ? 0.0f : 1.0f);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  use_framebuffer = true;
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  switch (_my_id) {
  case TEXTURE_LATTICE:
    glLineWidth (2.0f);

    glColor3f (0,0,0);
    glBegin (GL_LINES);
    glVertex2i (0, 0);  glVertex2i (_size, _size);//diagonal
    glVertex2i (0, 0);  glVertex2i (0, _size);//vertical
    glVertex2i (0, 0);  glVertex2i (_size, 0);//vertical
    glEnd ();
    glBegin (GL_LINE_STRIP);
    glVertex2i (0, 0);    
    for (i = 0; i < _size; i += 9) {
      if (i % 2)
        glVertex2i (0, i);    
      else
        glVertex2i (i, i);    
    }
    for (i = 0; i < _size; i += 9) {
      if (i % 2)
        glVertex2i (i, 0);    
      else
        glVertex2i (i, i);    
    }
    glEnd ();
    break;
  case TEXTURE_SOFT_CIRCLE:
    //Make a simple circle of light, bright in the center and fading out
    glEnable (GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    radius = ((float)_half) - 3;
    glBegin (GL_TRIANGLE_FAN);
    glColor4f (1, 1, 1, 1);
    glVertex2i (_half, _half);
    glColor4f (0, 0, 0, 0);
    for (i = 0; i <= 360; i++) {
      pos.x = sinf ((float)i * DEGREES_TO_RADIANS) * radius;
      pos.y = cosf ((float)i * DEGREES_TO_RADIANS) * radius;
      glVertex2i (_half + (int)pos.x, _half + (int)pos.y);
    }
    glEnd ();
    break;
  case TEXTURE_LIGHT:
    glEnable (GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    radius = ((float)_half) - 3;
    for (j = 0; j < 2; j++) {
      glBegin (GL_TRIANGLE_FAN);
      glColor4f (1, 1, 1, 1);
      glVertex2i (_half, _half);
      if (!j)
        radius = ((float)_half / 2);
      else
        radius = 8;
      glColor4f (1, 1, 1, 0);
      for (i = 0; i <= 360; i++) {
        pos.x = sinf ((float)i * DEGREES_TO_RADIANS) * radius;
        pos.y = cosf ((float)i * DEGREES_TO_RADIANS) * radius;
        glVertex2i (_half + (int)pos.x, _half + (int)pos.y);
      }
      glEnd ();
    }
    break;
  case TEXTURE_HEADLIGHT:
    DrawHeadlight ();
    break;
  case TEXTURE_LOGOS:
    i = 0;
    glDepthMask (false);
    glDisable (GL_BLEND);
    name_num = RandomVal (NAME_COUNT);
    prefix_num = RandomVal (PREFIX_COUNT);
    suffix_num = RandomVal (SUFFIX_COUNT);
    glColor3f (1,1,1);
    while (i < _size) {
      //randomly use a prefix OR suffix, but not both.  Too verbose.
      if (COIN_FLIP)
        RenderPrint (2, _size - i - LOGO_PIXELS / 4, RandomVal(), glRgba (1.0f), "%s%s", prefix[prefix_num], name[name_num]);
      else
        RenderPrint (2, _size - i - LOGO_PIXELS / 4, RandomVal(), glRgba (1.0f), "%s%s", name[name_num], suffix[suffix_num]);
      name_num = (name_num + 1) % NAME_COUNT;
      prefix_num = (prefix_num + 1) % PREFIX_COUNT;
      suffix_num = (suffix_num + 1) % SUFFIX_COUNT;
      i += LOGO_PIXELS;
    }
    break;
  case TEXTURE_TRIM:
    int     margin;
    y = 0;
    margin = MAX (TRIM_PIXELS / 4, 1);
    for (x = 0; x < _size; x += TRIM_PIXELS) 
      drawrect_simple (x + margin, y + margin, x + TRIM_PIXELS - margin, y + TRIM_PIXELS - margin, glRgba (1.0f), glRgba (0.5f));
    y += TRIM_PIXELS;
    for (x = 0; x < _size; x += TRIM_PIXELS * 2) 
      drawrect_simple (x + margin, y + margin, x + TRIM_PIXELS - margin, y + TRIM_PIXELS - margin, glRgba (1.0f), glRgba (0.5f));
    y += TRIM_PIXELS;
    for (x = 0; x < _size; x += TRIM_PIXELS * 3) 
      drawrect_simple (x + margin, y + margin, x + TRIM_PIXELS - margin, y + TRIM_PIXELS - margin, glRgba (1.0f), glRgba (0.5f));
    y += TRIM_PIXELS;
    for (x = 0; x < _size; x += TRIM_PIXELS) 
      drawrect_simple (x + margin, y + margin * 2, x + TRIM_PIXELS - margin, y + TRIM_PIXELS - margin, glRgba (1.0f), glRgba (0.5f));
    break;
  case TEXTURE_SKY:
    DrawSky ();
    break;
  default: //building textures
    DrawWindows ();
    break;
  }
  glPopMatrix ();
  //Now blit the finished image into our texture  
  if (use_framebuffer) {
    glBindTexture(GL_TEXTURE_2D, _glid);		
	  glCopyTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, _size, _size, 0);
  }
  if (_mipmap) {
    // Generate mip levels on the GPU from level 0 (already filled by the
    // glCopyTexImage2D above). The old path read the whole texture back to the
    // CPU (glGetTexImage) and rebuilt every level in software (gluBuild2DMipmaps)
    // -- ~85ms per texture on the Pi's V3D and the dominant city-load cost.
    glGenerateMipmap (GL_TEXTURE_2D);
	  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
	  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  } else
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  //cleanup and restore the viewport
  if (bake_to_fbo)
    glBindFramebuffer (GL_FRAMEBUFFER, 0);
  RenderResize ();
  _ready = true;
  lapsed = GetTimeInMillis () - start;
  build_time += lapsed;
    

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

unsigned TextureId (int id)
{

  for (CTexture* t = head; t; t = t->_next) {
    if (t->_my_id == id)
      return t->_glid;
  }
  return 0;

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

unsigned TextureRandomBuilding (int index)
{

  index = abs (index) % BUILDING_COUNT;
  return TextureId (TEXTURE_BUILDING1 + index);

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void TextureReset (void)
{

  textures_done = false;
  build_time = 0;
  for (CTexture* t = head; t; t = t->_next)
    t->Clear ();
  memset (prefix_used, 0, sizeof (prefix_used));
  memset (name_used, 0, sizeof (name_used));
  memset (suffix_used, 0, sizeof (suffix_used));

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

bool TextureReady ()
{

  return textures_done;

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void TextureUpdate (void)
{

  // The bloom texture is now captured from the main frame in RenderUpdate(),
  // so TextureUpdate() only has to build the static textures once at startup.
  if (textures_done)
    return;
  // The building-window textures bake in parallel in a single pass (all cores);
  // the remaining textures stay on the one-per-frame GL path below.
  for (CTexture* t = head; t; t = t->_next)
    if (t->_my_id >= TEXTURE_BUILDING1 && t->_my_id <= TEXTURE_BUILDING9 && !t->_ready) {
      bake_building_textures_parallel ();
      break;
    }
  for (CTexture* t = head; t; t = t->_next) {
    if (!t->_ready) {
      t->Rebuild();
      return;
    }
  }
  textures_done = true;

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void TextureTerm (void)
{

  CTexture*    t;

  while (head) {
    t = head->_next;
    delete head;
    head = t;
  }

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

void TextureInit (void)
{

  new CTexture (TEXTURE_SKY,          512,  true,  false, false);
  new CTexture (TEXTURE_LATTICE,      128,  true,  true,  true);
  new CTexture (TEXTURE_LIGHT,        128,  false, false, true);
  new CTexture (TEXTURE_SOFT_CIRCLE,  128,  false, false, true);
  new CTexture (TEXTURE_HEADLIGHT,    128,  false, false, true);
  new CTexture (TEXTURE_TRIM,  TRIM_RESOLUTION,  true, false, false);
  new CTexture (TEXTURE_LOGOS, LOGO_RESOLUTION,  true, false, true);
  for (int i = TEXTURE_BUILDING1; i <= TEXTURE_BUILDING9; i++)
    new CTexture (i, 512, true, false, false);
  // TEXTURE_BLOOM is intentionally not built here. Bloom is now a post-process
  // shader pass over the main FBO (see RenderUpdate/RenderBloom); the old
  // fixed-function do_effects() path that sampled this 2048x2048 texture is dead
  // code. Building it cost ~170ms at startup (a junk DrawWindows() fill plus a
  // 16MB glCopyTexImage2D + mipmap) for a texture nothing ever samples.
  // int  names = PREFIX_COUNT * NAME_COUNT + SUFFIX_COUNT * NAME_COUNT;

}

/*-----------------------------------------------------------------------------

-----------------------------------------------------------------------------*/

float TextureProgress ()
{

  int total = 0;
  int ready = 0;
  for (CTexture* t = head; t; t = t->_next) {
    total++;
    if (t->_ready)
      ready++;
  }
  return total > 0 ? (float)ready / total : 1.0f;

}
