/*
 * Lattice screensaver - Wayland/EGL port
 * Original Copyright (C) 1999-2010 Terence M. Welsh (GPL-2.0+)
 * Wayland port: native Wayland client with EGL/OpenGL, no X11
 *
 * Build:  make
 * Run:    ./lattice [--preset NAME] [options]
 * Quit:   Escape or close button
 * Keys:   F / F11 = toggle fullscreen, Q / Esc = quit
 *
 * Presets (--preset NAME):
 *   regular    — default look, random colours
 *   chainmail  — dense smooth rings, silver material
 *   brassmesh  — square-profile rings, brass material  ← default
 *   computer   — chunky dense rings, random colours
 *   slick      — large thick smooth rings, pearl material
 *   tasty      — same as slick, slightly sparser
 *
 * Per-parameter overrides (applied after preset):
 *   --speed N      (1-100)
 *   --depth N      (1-10)
 *   --density N    (1-100)
 *   --thick N      (1-100)
 *   --fov N        (10-150 degrees)
 *   --longitude N  (4-100)
 *   --latitude N   (2-100)
 *   --pathrand N   (1-10)
 *   --smooth / --no-smooth
 *   --fog  / --no-fog
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <vector>
#include <unistd.h>
#include <time.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glext.h>

#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-client-protocol.h"
#include "texture.h"

/* -------------------------------------------------------------------------
 * Math library (replaces rsMath)
 * ---------------------------------------------------------------------- */

static inline float rsRandf(float max) {
    return ((float)rand() / (float)RAND_MAX) * max;
}

static inline int rsRandi(int max) {
    return (max > 0) ? rand() % max : 0;
}

class rsVec {
public:
    float v[3];

    rsVec() { v[0]=v[1]=v[2]=0.0f; }
    rsVec(float x, float y, float z) { v[0]=x; v[1]=y; v[2]=z; }

    float& operator[](int i)             { return v[i]; }
    const float& operator[](int i) const { return v[i]; }

    rsVec operator+(const rsVec& o) const {
        return rsVec(v[0]+o.v[0], v[1]+o.v[1], v[2]+o.v[2]);
    }
    rsVec operator-(const rsVec& o) const {
        return rsVec(v[0]-o.v[0], v[1]-o.v[1], v[2]-o.v[2]);
    }
    rsVec& operator+=(const rsVec& o) {
        v[0]+=o.v[0]; v[1]+=o.v[1]; v[2]+=o.v[2]; return *this;
    }

    float dot(const rsVec& o) const {
        return v[0]*o.v[0] + v[1]*o.v[1] + v[2]*o.v[2];
    }
    void cross(const rsVec& a, const rsVec& b) {
        v[0] = a.v[1]*b.v[2] - a.v[2]*b.v[1];
        v[1] = a.v[2]*b.v[0] - a.v[0]*b.v[2];
        v[2] = a.v[0]*b.v[1] - a.v[1]*b.v[0];
    }
    float length() const {
        return sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    }
    float normalize() {
        float len = length();
        if (len > 1e-7f) { v[0]/=len; v[1]/=len; v[2]/=len; }
        return len;
    }
    void scale(float s) { v[0]*=s; v[1]*=s; v[2]*=s; }
};

class rsQuat {
public:
    float w, x, y, z;
    rsQuat() : w(1.0f), x(0.0f), y(0.0f), z(0.0f) {}

    void make(float angle, float ax, float ay, float az) {
        float s = sinf(angle * 0.5f);
        w = cosf(angle * 0.5f);
        x = ax * s; y = ay * s; z = az * s;
    }

    /* this = rhs * this */
    void preMult(const rsQuat& r) {
        float nw = r.w*w - r.x*x - r.y*y - r.z*z;
        float nx = r.w*x + r.x*w + r.y*z - r.z*y;
        float ny = r.w*y - r.x*z + r.y*w + r.z*x;
        float nz = r.w*z + r.x*y - r.y*x + r.z*w;
        w=nw; x=nx; y=ny; z=nz; _renorm();
    }

    /* this = this * rhs */
    void postMult(const rsQuat& r) {
        float nw = w*r.w - x*r.x - y*r.y - z*r.z;
        float nx = w*r.x + x*r.w + y*r.z - z*r.y;
        float ny = w*r.y - x*r.z + y*r.w + z*r.x;
        float nz = w*r.z + x*r.y - y*r.x + z*r.w;
        w=nw; x=nx; y=ny; z=nz; _renorm();
    }

    /* Column-major 4x4 rotation matrix for OpenGL */
    void toMat(float* m) const {
        float xx=x*x, yy=y*y, zz=z*z;
        float xy=x*y, xz=x*z, yz=y*z;
        float wx=w*x, wy=w*y, wz=w*z;
        m[0]=1-2*(yy+zz); m[4]=2*(xy-wz);   m[8] =2*(xz+wy);   m[12]=0;
        m[1]=2*(xy+wz);   m[5]=1-2*(xx+zz); m[9] =2*(yz-wx);   m[13]=0;
        m[2]=2*(xz-wy);   m[6]=2*(yz+wx);   m[10]=1-2*(xx+yy); m[14]=0;
        m[3]=0;           m[7]=0;            m[11]=0;            m[15]=1;
    }
private:
    void _renorm() {
        float len = sqrtf(w*w+x*x+y*y+z*z);
        if (len > 1e-7f) { w/=len; x/=len; y/=len; z/=len; }
    }
};

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

/* -------------------------------------------------------------------------
 * VBO infrastructure (GL 1.5, universally supported)
 *
 * All torus geometry is baked into per-object VBOs at init time:
 * positions, normals, texcoords, and colours are pre-transformed by the
 * ring's local matrix (translate + rotate) so each visible cell costs
 * exactly one glDrawArrays call instead of up to six display-list chains.
 *
 * On panfrost (and any driver that emulates display lists in software),
 * this eliminates the Mesa compat-layer overhead entirely.  On desktop
 * GPUs the path is equivalent or slightly faster — VBOs have been the
 * canonical draw path since OpenGL 1.5 (2003).
 * ---------------------------------------------------------------------- */

struct GVertex {           /* 48 bytes, matches glVertexPointer offsets below */
    float pos[3];          /* offset  0 */
    float normal[3];       /* offset 12 */
    float tc[2];           /* offset 24 */
    float color[4];        /* offset 32 — alpha always 1 */
};

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER  0x8892
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW   0x88B4
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW  0x88B8
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif

typedef void (*PFN_GenBuffers_t)   (GLsizei, GLuint *);
typedef void (*PFN_BindBuffer_t)   (GLenum, GLuint);
typedef void (*PFN_BufferData_t)   (GLenum, ptrdiff_t, const void *, GLenum);
typedef void (*PFN_DeleteBuffers_t)(GLsizei, const GLuint *);

static PFN_GenBuffers_t    fn_GenBuffers    = nullptr;
static PFN_BindBuffer_t    fn_BindBuffer    = nullptr;
static PFN_BufferData_t    fn_BufferData    = nullptr;
static PFN_DeleteBuffers_t fn_DeleteBuffers = nullptr;

static void load_vbo_fns() {
    fn_GenBuffers    = (PFN_GenBuffers_t)   eglGetProcAddress("glGenBuffers");
    fn_BindBuffer    = (PFN_BindBuffer_t)   eglGetProcAddress("glBindBuffer");
    fn_BufferData    = (PFN_BufferData_t)   eglGetProcAddress("glBufferData");
    fn_DeleteBuffers = (PFN_DeleteBuffers_t)eglGetProcAddress("glDeleteBuffers");
}

/* Shader + instancing function pointers (GL 2.0 / GL_ARB_instanced_arrays) */
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
static PFNGLUNIFORM1FPROC                 fn_Uniform1f             = nullptr;
static PFNGLUNIFORM3FVPROC                fn_Uniform3fv            = nullptr;
static PFNGLUNIFORMMATRIX4FVPROC          fn_UniformMatrix4fv      = nullptr;
static PFNGLENABLEVERTEXATTRIBARRAYPROC   fn_EnableVertexAttribArray  = nullptr;
static PFNGLDISABLEVERTEXATTRIBARRAYPROC  fn_DisableVertexAttribArray = nullptr;
static PFNGLVERTEXATTRIBPOINTERPROC       fn_VertexAttribPointer   = nullptr;
static PFNGLVERTEXATTRIBDIVISORPROC       fn_VertexAttribDivisor   = nullptr;
static PFNGLDRAWARRAYSINSTANCEDPROC       fn_DrawArraysInstanced   = nullptr;
static PFNGLDRAWELEMENTSINSTANCEDPROC     fn_DrawElementsInstanced = nullptr;
static PFNGLDELETESHADERPROC              fn_DeleteShader          = nullptr;
static PFNGLDELETEPROGRAMPROC             fn_DeleteProgram         = nullptr;

static void load_shader_fns() {
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
    fn_Uniform1f            = (PFNGLUNIFORM1FPROC)               eglGetProcAddress("glUniform1f");
    fn_Uniform3fv           = (PFNGLUNIFORM3FVPROC)              eglGetProcAddress("glUniform3fv");
    fn_UniformMatrix4fv     = (PFNGLUNIFORMMATRIX4FVPROC)        eglGetProcAddress("glUniformMatrix4fv");
    fn_EnableVertexAttribArray  = (PFNGLENABLEVERTEXATTRIBARRAYPROC) eglGetProcAddress("glEnableVertexAttribArray");
    fn_DisableVertexAttribArray = (PFNGLDISABLEVERTEXATTRIBARRAYPROC)eglGetProcAddress("glDisableVertexAttribArray");
    fn_VertexAttribPointer  = (PFNGLVERTEXATTRIBPOINTERPROC)     eglGetProcAddress("glVertexAttribPointer");
    fn_DeleteShader         = (PFNGLDELETESHADERPROC)            eglGetProcAddress("glDeleteShader");
    fn_DeleteProgram        = (PFNGLDELETEPROGRAMPROC)           eglGetProcAddress("glDeleteProgram");
    /* glVertexAttribDivisor: core in GL 3.3; also via GL_ARB_instanced_arrays on GL 3.1 */
    fn_VertexAttribDivisor  = (PFNGLVERTEXATTRIBDIVISORPROC)
        eglGetProcAddress("glVertexAttribDivisor");
    if (!fn_VertexAttribDivisor)
        fn_VertexAttribDivisor = (PFNGLVERTEXATTRIBDIVISORPROC)
            eglGetProcAddress("glVertexAttribDivisorARB");
    fn_DrawArraysInstanced  = (PFNGLDRAWARRAYSINSTANCEDPROC)
        eglGetProcAddress("glDrawArraysInstanced");
    if (!fn_DrawArraysInstanced)
        fn_DrawArraysInstanced = (PFNGLDRAWARRAYSINSTANCEDPROC)
            eglGetProcAddress("glDrawArraysInstancedARB");
    fn_DrawElementsInstanced = (PFNGLDRAWELEMENTSINSTANCEDPROC)
        eglGetProcAddress("glDrawElementsInstanced");
    if (!fn_DrawElementsInstanced)
        fn_DrawElementsInstanced = (PFNGLDRAWELEMENTSINSTANCEDPROC)
            eglGetProcAddress("glDrawElementsInstancedARB");
}

/* Column-major 4×4 matrix helpers (match OpenGL convention exactly) */
static void mat4_identity(float M[16]) {
    memset(M, 0, 64); M[0]=M[5]=M[10]=M[15]=1.0f;
}
static void mat4_translate(float M[16], float tx, float ty, float tz) {
    mat4_identity(M); M[12]=tx; M[13]=ty; M[14]=tz;
}
static void mat4_rotate(float M[16], float deg, float ax, float ay, float az) {
    float a=deg*0.0174532925f, c=cosf(a), s=sinf(a), ic=1.0f-c;
    float len=sqrtf(ax*ax+ay*ay+az*az); ax/=len; ay/=len; az/=len;
    mat4_identity(M);
    M[0] =ax*ax*ic+c;    M[1] =ax*ay*ic+az*s;  M[2] =ax*az*ic-ay*s;
    M[4] =ay*ax*ic-az*s; M[5] =ay*ay*ic+c;     M[6] =ay*az*ic+ax*s;
    M[8] =az*ax*ic+ay*s; M[9] =az*ay*ic-ax*s;  M[10]=az*az*ic+c;
}
static void mat4_mul(float C[16], const float A[16], const float B[16]) {
    for (int c=0; c<4; c++)
        for (int r=0; r<4; r++) {
            float s=0; for (int k=0; k<4; k++) s+=A[k*4+r]*B[c*4+k];
            C[c*4+r]=s;
        }
}

/* -------------------------------------------------------------------------
 * Camera (ported from camera.h / camera.cpp)
 * ---------------------------------------------------------------------- */

class camera {
public:
    float farplane;
    float cullVec[4][3];

    camera() {}
    ~camera() {}

    void init(float* mat, float f) {
        float temp;
        farplane = f;
        temp = atanf(1.0f / mat[5]);
        cullVec[0][0]=0.0f; cullVec[0][1]= cosf(temp); cullVec[0][2]=-sinf(temp);
        cullVec[1][0]=0.0f; cullVec[1][1]=-cullVec[0][1]; cullVec[1][2]=cullVec[0][2];
        temp = atanf(1.0f / mat[0]);
        cullVec[2][0]= cosf(temp); cullVec[2][1]=0.0f; cullVec[2][2]=-sinf(temp);
        cullVec[3][0]=-cullVec[2][0]; cullVec[3][1]=0.0f; cullVec[3][2]=cullVec[2][2];
    }

    bool inViewVolume(const float pos[3], float radius) {
        if (pos[2] < -(farplane + radius)) return false;
        
        // Plane 0 & 1 (y-axis culling, x-component is 0.0f)
        float y_term = pos[1] * cullVec[0][1];
        float z_term_01 = pos[2] * cullVec[0][2];
        if (y_term + z_term_01 < -radius) return false;
        if (-y_term + z_term_01 < -radius) return false;
        
        // Plane 2 & 3 (x-axis culling, y-component is 0.0f)
        float x_term = pos[0] * cullVec[2][0];
        float z_term_23 = pos[2] * cullVec[2][2];
        if (x_term + z_term_23 < -radius) return false;
        if (-x_term + z_term_23 < -radius) return false;
        
        return true;
    }
};

/* -------------------------------------------------------------------------
 * Screensaver constants
 * ---------------------------------------------------------------------- */

#define PI      3.14159265359f
#define PIx2    6.28318530718f
#define D2R     0.0174532925f
#define NUMOBJECTS 20
#define LATSIZE    12

/* -------------------------------------------------------------------------
 * Presets — faithful port of original Windows dialog "Defaults" buttons
 * texType: 0=none 1=industrial 2=crystal 3=chrome 4=brass
 *          5=shiny 6=ghostly 7=circuits 8=doughnuts
 * ---------------------------------------------------------------------- */

struct Preset {
    const char *name;
    int longitude, latitude, thick, density, depth, fov, pathrand, speed;
    bool smooth, fog;
    int texType;
};

static const Preset PRESETS[] = {
    /* name        lon lat  thk  den dep fov  pr  sp   smo    fog  tex */
    {"regular",     16,  8,  50,  50,  5, 90,  7, 10, false, true,  0},
    {"chainmail",   24, 12,  50,  80,  4, 90,  7, 10, true,  true,  3},
    {"brassmesh",    4,  4,  40,  50,  5, 90,  7, 10, false, true,  4},
    {"computer",     4,  6,  70,  90,  4, 90,  7, 10, false, true,  7},
    {"slick",       24, 12, 100,  30,  5, 90,  7, 10, true,  true,  5},
    {"tasty",       24, 12, 100,  25,  5, 90,  7, 10, true,  true,  8},
    {NULL}
};

static int  dLongitude = 4;
static int  dLatitude  = 4;
static int  dThick     = 40;
static int  dDensity   = 50;
static int  dDepth     = 5;
static int  dFov       = 90;
static int  dPathrand  = 7;
static int  dSpeed     = 10;
static bool dSmooth    = false;
static bool dFog       = true;
static int  dTexture   = 4;    /* brassmesh default */
static const char *dPresetName = "brassmesh";
static int         g_preset_idx = 2;   /* brassmesh */

static unsigned int texture_id[2];

/* -------------------------------------------------------------------------
 * Screensaver state
 * ---------------------------------------------------------------------- */

static float aspectRatio;
static float frameTime = 0.0f;
static unsigned int latticeGrid[LATSIZE][LATSIZE][LATSIZE];
static GLuint g_all_vbo;                   /* single buffer for all 20 objects */
static int    g_obj_start [NUMOBJECTS];    /* first vertex of each object in VBO */
static int    g_obj_vcount[NUMOBJECTS];    /* vertex count of each object */
static GLuint g_all_ibo;                   /* index buffer for all 20 objects */
static int    g_obj_istart[NUMOBJECTS];    /* first index of each object in IBO */
static int    g_obj_icount[NUMOBJECTS];    /* index count of each object */
static GLuint g_inst_vbo  = 0;            /* per-frame per-instance translation data */
static GLuint g_prog      = 0;            /* shader program */
static float  g_proj_mat[16];             /* projection matrix (stored by setupProjection) */
/* Uniform locations (valid after build_shader_program) */
static GLint  g_u_proj, g_u_mv, g_u_lit, g_u_shin, g_u_ldir;
static GLint  g_u_smap, g_u_fog, g_u_fog0, g_u_fog1, g_u_tex, g_u_samp;
static float bPnt[10][6];
static float path[7][6];
static int transitions[20][6] = {
    { 1, 2,12, 4,14, 8}, { 0, 3,15, 7, 7, 7}, { 3, 4,14, 0, 7,16}, { 2, 1,15, 7, 7, 7},
    { 5,10,12,17,17,17}, { 4, 3,13,11, 9,17}, {12, 4,10,17,17,17}, { 2, 0,14, 8,16,19},
    { 1, 3,15, 7, 7, 7}, { 4,10,12,17,17,17}, {11, 4,12,17,17,17}, {10, 5,15,13,17,18},
    {13,10, 4,17,17,17}, {12, 1,11, 5, 6,17}, {15, 2,12, 0, 7,19}, {14, 3, 1, 7, 7, 7},
    { 3, 1,15, 7, 7, 7}, { 5,11,13, 6, 9,18}, {10, 4,12,17,17,17}, {15, 1, 3, 7, 7, 7}
};
static int   globalxyz[3];
static int   lastBorder;
static int   segments;
static camera *theCamera = NULL;

/* -------------------------------------------------------------------------
 * Physics simulation state
 * Lifted from draw() statics so update_physics / render_scene can share them.
 * ---------------------------------------------------------------------- */
static rsVec  g_xyz, g_oldxyz, g_oldDir, g_oldAngvel;
static float  g_rotMat[16];
static rsQuat g_quat;
static int    g_flymode = 1, g_seg = 0;
static float  g_where = 0.0f, g_flymodeChange = 20.0f;
static float  g_rollVel = 0.0f, g_rollAcc = 0.0f;
static float  g_rollChange = 0.0f;  /* set in initSaver() */
static int    g_drawDepth  = 0;     /* set in initSaver() */

/* Physics run at exactly 60 fps worth of time per real second, regardless of
 * display refresh rate.  The wall-clock timer drives a fixed-step accumulator;
 * render_scene() then draws whatever state the physics last produced. */
static const float SIM_DT      = 1.0f / 60.0f;
static float       g_sim_accum = 0.0f;
static rsTimer     g_wall_timer;

/* -------------------------------------------------------------------------
 * Wayland + EGL state
 * ---------------------------------------------------------------------- */

static struct wl_display    *g_display;
static struct wl_compositor *g_compositor;
static struct xdg_wm_base   *g_wm_base;
static struct wl_surface    *g_surface;
static struct xdg_surface   *g_xdg_surface;
static struct xdg_toplevel  *g_toplevel;
static struct wl_seat       *g_seat;
static struct wl_keyboard   *g_keyboard;
static struct wl_egl_window                *g_egl_window;
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

static int g_win_width    = 800;
static int g_win_height   = 600;
static int g_configured   = 0;
static int g_running      = 1;
static int g_needs_resize = 0;
static int g_new_width, g_new_height;

/* -------------------------------------------------------------------------
 * Utility
 * ---------------------------------------------------------------------- */

static int myMod(int x) {
    while (x < 0) x += LATSIZE;
    return x % LATSIZE;
}

static float interpolate(float a, float b, float c, float d, float where) {
    float q = 2.0f*(a-c)+b+d;
    float r = 3.0f*(c-a)-2.0f*b-d;
    return (where*where*where*q) + (where*where*r) + (where*b) + a;
}

/* -------------------------------------------------------------------------
 * Torus geometry — CPU side only (no GL calls; used by buildLatticeVBOs)
 * ---------------------------------------------------------------------- */

/* Build one torus into verts+indices with M's transform baked in.
 *
 * Smooth: lat×lon unique vertices, lat×lon×6 indices.
 *   The GPU Post-Transform Cache reuses each shaded vertex ~6 times → ~6×
 *   fewer vertex-shader invocations vs. the old non-indexed expansion.
 *
 * Flat: 2×lat×lon vertices (strip pairs with face normals), lat×lon×6 indices.
 *   Adjacent lat-strips cannot share a border row (theta-normals differ), so
 *   we get 4 unique corners per quad → 4 vs 6 verts per quad = 1.5× saving. */
static void buildTorusCPU(std::vector<GVertex> &verts,
                           std::vector<unsigned int> &indices,
                           int smooth, int lon, int lat,
                           float cr, float tr,
                           const float M[16], const float col[4])
{
    float vstep = 1.0f / (float)lat;
    float ustep = (float)(int)(cr/tr + 0.5f) / (float)lon;

    auto xfv = [&](GVertex &v) {
        float px=v.pos[0],py=v.pos[1],pz=v.pos[2];
        float nx=v.normal[0],ny=v.normal[1],nz=v.normal[2];
        v.pos[0]=M[0]*px+M[4]*py+M[8]*pz +M[12];
        v.pos[1]=M[1]*px+M[5]*py+M[9]*pz +M[13];
        v.pos[2]=M[2]*px+M[6]*py+M[10]*pz+M[14];
        v.normal[0]=M[0]*nx+M[4]*ny+M[8]*nz;
        v.normal[1]=M[1]*nx+M[5]*ny+M[9]*nz;
        v.normal[2]=M[2]*nx+M[6]*ny+M[10]*nz;
    };

    if (smooth) {
        /* ------ smooth: lat×lon unique shared vertices ------------------- */
        unsigned int vbase = (unsigned int)verts.size();
        for (int i = 0; i < lat; i++) {
            float theta = PIx2*(float)i/(float)lat;
            float cosn=cosf(theta), sinn=sinf(theta);
            float r=cr+tr*cosn, z=tr*sinn;
            float vi=(float)i*vstep;
            for (int j = 0; j < lon; j++) {
                float phi=PIx2*(float)j/(float)lon;
                float cosa=cosf(phi), sina=sinf(phi);
                GVertex v;
                v.pos   [0]=r*cosa;    v.pos   [1]=r*sina;    v.pos   [2]=z;
                v.normal[0]=cosn*cosa; v.normal[1]=cosn*sina; v.normal[2]=sinn;
                v.tc[0]=(float)j*ustep; v.tc[1]=vi;
                v.color[0]=col[0]; v.color[1]=col[1]; v.color[2]=col[2]; v.color[3]=col[3];
                xfv(v);
                verts.push_back(v);
            }
        }
        /* Quad (i,j): rows i and (i+1)%lat, columns j and (j+1)%lon.
         * Winding matches old strip-to-triangle expansion (verified above). */
        for (int i = 0; i < lat; i++) {
            unsigned int r0 = (unsigned int)(i * lon);
            unsigned int r1 = (unsigned int)(((i+1)%lat) * lon);
            for (int j = 0; j < lon; j++) {
                unsigned int jn = (unsigned int)((j+1)%lon);
                unsigned int r0c0=vbase+r0+(unsigned int)j,  r0c1=vbase+r0+jn;
                unsigned int r1c0=vbase+r1+(unsigned int)j,  r1c1=vbase+r1+jn;
                indices.push_back(r1c0); indices.push_back(r0c0); indices.push_back(r1c1);
                indices.push_back(r1c1); indices.push_back(r0c0); indices.push_back(r0c1);
            }
        }
    } else {
        /* ------ flat: 2 vertices per column per lat-strip ---------------- */
        for (int i = 0; i < lat; i++) {
            float ti =PIx2*(float)i    /(float)lat;
            float ti1=PIx2*(float)(i+1)/(float)lat;
            float cosn=cosf(ti), sinn=sinf(ti), cosnn=cosf(ti1), sinnn=sinf(ti1);
            float r=cr+tr*cosn, rr=cr+tr*cosnn, z=tr*sinn, zz=tr*sinnn;
            /* flat theta normal: midpoint of this strip */
            float mid=PIx2*((float)i+0.5f)/(float)lat;
            float nc=cosf(mid), ns=sinf(mid);
            float vi=(float)i*vstep, vi1=(float)(i+1)*vstep;

            unsigned int sbase=(unsigned int)verts.size();
            float old_c=1,old_s=0,old_nc=1,old_ns=0;
            for (int j=0; j<lon; j++) {
                float phi=PIx2*(float)j/(float)lon;
                float cosa=cosf(phi), sina=sinf(phi);
                float pn=PIx2*((float)j-0.5f)/(float)lon;
                float ncosa=cosf(pn), nsina=sinf(pn);
                if (j==0){old_c=cosa;old_s=sina;old_nc=ncosa;old_ns=nsina;}
                /* upper = row i+1 (rr,zz), lower = row i (r,z) */
                GVertex vu={{cosa*rr,sina*rr,zz},{nc*ncosa,nc*nsina,ns},{(float)j*ustep,vi1},{col[0],col[1],col[2],col[3]}};
                GVertex vl={{cosa*r, sina*r, z },{nc*ncosa,nc*nsina,ns},{(float)j*ustep,vi },{col[0],col[1],col[2],col[3]}};
                xfv(vu); xfv(vl);
                verts.push_back(vu);   /* sbase + 2*j+0 */
                verts.push_back(vl);   /* sbase + 2*j+1 */
            }
            (void)old_c; (void)old_s; (void)old_nc; (void)old_ns;

            for (int j=0; j<lon; j++) {
                unsigned int jn=(unsigned int)((j+1)%lon);
                unsigned int u0=sbase+(unsigned int)(2*j);   /* upper at j   */
                unsigned int l0=sbase+(unsigned int)(2*j+1); /* lower at j   */
                unsigned int u1=sbase+2*jn;                  /* upper at j+1 */
                unsigned int l1=sbase+2*jn+1;                /* lower at j+1 */
                /* winding: (upper@j, lower@j, upper@j+1), (upper@j+1, lower@j, lower@j+1) */
                indices.push_back(u0); indices.push_back(l0); indices.push_back(u1);
                indices.push_back(u1); indices.push_back(l0); indices.push_back(l1);
            }
        }
    }
}

/* buildLatticeVBOs — replaces makeLatticeObjects.
 * Replicates the exact same rand() call sequence so colours and ring
 * orientations are identical to what display lists would have produced.
 * Transforms are baked into vertex positions/normals at build time.
 * All 20 objects are packed into one VBO so render_scene binds once
 * and varies only the start/count per glDrawArrays call. */
static void buildLatticeVBOs() {
    std::vector<GVertex>       all_verts;
    std::vector<unsigned int>  all_indices;
    all_verts.reserve(NUMOBJECTS * 576);
    all_indices.reserve(NUMOBJECTS * 576 * 6);

    float thick = (float)dThick * 0.001f;
    int d = 0;

    auto pickCol = [&](float col[4]) {
        if (dTexture == 0 || dTexture >= 5) {
            col[0]=rsRandf(1.0f); col[1]=rsRandf(1.0f);
            col[2]=rsRandf(1.0f); col[3]=1.0f;
        } else if (dTexture == 1) {
            rsRandi(2);
            col[0]=col[1]=col[2]=col[3]=1.0f;
        } else {
            col[0]=col[1]=col[2]=col[3]=1.0f;
        }
    };

    for (int i = 0; i < NUMOBJECTS; i++) {
        g_obj_start [i] = (int)all_verts.size();
        g_obj_vcount[i] = 0;
        g_obj_istart[i] = (int)all_indices.size();
        g_obj_icount[i] = 0;

        float col[4], T[16], R[16], M[16];

#define RING(rotExpr)                                                    \
        if (d < dDensity) {                                              \
            pickCol(col); rotExpr;                                       \
            size_t vbefore = all_verts.size();                           \
            size_t ibefore = all_indices.size();                         \
            buildTorusCPU(all_verts, all_indices, dSmooth, dLongitude,  \
                          dLatitude, 0.36f-thick, thick, M, col);        \
            g_obj_vcount[i] += (int)(all_verts.size()   - vbefore);     \
            g_obj_icount[i] += (int)(all_indices.size() - ibefore);     \
        }                                                                \
        d = (d+37) % 100;

        RING(mat4_translate(T,-0.25f,-0.25f,-0.25f);
             if(rsRandi(2)){mat4_rotate(R,180.f,1,0,0);mat4_mul(M,T,R);}
             else memcpy(M,T,64);)

        RING(mat4_translate(T,0.25f,-0.25f,-0.25f);
             mat4_rotate(R,rsRandi(2)?90.f:-90.f,1,0,0); mat4_mul(M,T,R);)

        RING(mat4_translate(T,0.25f,-0.25f,0.25f);
             mat4_rotate(R,rsRandi(2)?90.f:-90.f,0,1,0); mat4_mul(M,T,R);)

        RING(mat4_translate(T,0.25f,0.25f,0.25f);
             if(rsRandi(2)){mat4_rotate(R,180.f,1,0,0);mat4_mul(M,T,R);}
             else memcpy(M,T,64);)

        RING(mat4_translate(T,-0.25f,0.25f,0.25f);
             mat4_rotate(R,rsRandi(2)?90.f:-90.f,1,0,0); mat4_mul(M,T,R);)

        /* Ring 5 — no trailing d advance in the original before glEndList */
        if (d < dDensity) {
            pickCol(col);
            mat4_translate(T,-0.25f,0.25f,-0.25f);
            mat4_rotate(R,rsRandi(2)?90.f:-90.f,0,1,0); mat4_mul(M,T,R);
            size_t vbefore = all_verts.size();
            size_t ibefore = all_indices.size();
            buildTorusCPU(all_verts,all_indices,dSmooth,dLongitude,dLatitude,0.36f-thick,thick,M,col);
            g_obj_vcount[i] += (int)(all_verts.size()   - vbefore);
            g_obj_icount[i] += (int)(all_indices.size() - ibefore);
        }
        d = (d+37) % 100;   /* matches the d advance after glEndList in original */
#undef RING
    }

    fn_GenBuffers(1, &g_all_vbo);
    if (!all_verts.empty()) {
        fn_BindBuffer(GL_ARRAY_BUFFER, g_all_vbo);
        fn_BufferData(GL_ARRAY_BUFFER, (ptrdiff_t)(all_verts.size()*sizeof(GVertex)),
                      all_verts.data(), GL_STATIC_DRAW);
        fn_BindBuffer(GL_ARRAY_BUFFER, 0);
    }
    fn_GenBuffers(1, &g_all_ibo);
    if (!all_indices.empty()) {
        fn_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_all_ibo);
        fn_BufferData(GL_ELEMENT_ARRAY_BUFFER,
                      (ptrdiff_t)(all_indices.size()*sizeof(unsigned int)),
                      all_indices.data(), GL_STATIC_DRAW);
        fn_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
}

/* -------------------------------------------------------------------------
 * Path navigation
 * ---------------------------------------------------------------------- */

static void reconfigure() {
    int i, j, newBorder, positive;

    for (i = 0; i < 6; i++)
        path[0][i] = path[segments][i];

    if (lastBorder < 6) {
        if ((path[0][3]+path[0][4]+path[0][5]) > 0.0f) {
            positive = 1; globalxyz[lastBorder/2]++;
        } else {
            positive = 0; globalxyz[lastBorder/2]--;
        }
    } else {
        positive = (path[0][3] > 0.0f) ? 1 : 0;
        if (positive) globalxyz[0]++; else globalxyz[0]--;
        if (path[0][4] > 0.0f) globalxyz[1]++; else globalxyz[1]--;
        if (path[0][5] > 0.0f) globalxyz[2]++; else globalxyz[2]--;
    }

    if (!rsRandi(11 - dPathrand)) {
        if (!positive) lastBorder += 10;
        newBorder = transitions[lastBorder][rsRandi(6)];
        positive = (newBorder < 10) ? 1 : 0;
        if (!positive) newBorder -= 10;
        for (i = 0; i < 6; i++) path[1][i] = bPnt[newBorder][i];
        if (!positive) {
            if (newBorder < 6) path[1][newBorder/2] *= -1.0f;
            else for (i = 0; i < 3; i++) path[1][i] *= -1.0f;
            for (i = 3; i < 6; i++) path[1][i] *= -1.0f;
        }
        for (i = 0; i < 3; i++) path[1][i] += globalxyz[i];
        lastBorder = newBorder;
    } else {
        newBorder = lastBorder;
        for (i = 0; i < 6; i++) path[1][i] = bPnt[newBorder][i];
        i = newBorder / 2;
        if (!positive) {
            if (newBorder < 6) path[1][i] *= -1.0f;
            else { path[1][0]*=-1; path[1][1]*=-1; path[1][2]*=-1; }
            path[1][3]*=-1; path[1][4]*=-1; path[1][5]*=-1;
        }
        for (j = 0; j < 3; j++) {
            path[1][j] += globalxyz[j];
            if ((newBorder < 6) && (j != 1))
                path[1][j] += rsRandf(0.15f) - 0.075f;
        }
        if (newBorder >= 6) path[1][0] += rsRandf(0.1f) - 0.05f;
    }
    segments = 1;
}

/* -------------------------------------------------------------------------
 * Projection setup (also called on resize)
 * ---------------------------------------------------------------------- */

static void setupProjection(int w, int h) {
    aspectRatio = (float)w / (float)h;

    float mat[16];
    float f = cosf((float)dFov * 0.5f * D2R) / sinf((float)dFov * 0.5f * D2R);
    memset(mat, 0, sizeof(mat));
    mat[0]  = f / aspectRatio;   /* dFov is vertical FOV; horizontal widens with aspect ratio */
    mat[5]  = f;
    mat[10] = -1.0f - 0.02f / (float)dDepth;
    mat[11] = -1.0f;
    mat[14] = -(0.02f + 0.0002f / (float)dDepth);

    memcpy(g_proj_mat, mat, sizeof(mat));   /* used by instancing shaders each frame */

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(mat);
    glViewport(0, 0, w, h);

    delete theCamera;
    theCamera = new camera;
    theCamera->init(mat, (float)dDepth);

    glMatrixMode(GL_MODELVIEW);
}

/* -------------------------------------------------------------------------
 * Physics update  (always called with frameTime == SIM_DT)
 * ---------------------------------------------------------------------- */

static void update_physics() {
    rsVec xyz, dir, angvel, tempVec;
    rsQuat newQuat;

    g_where += (float)dSpeed * 0.05f * frameTime;
    if (g_where >= 1.0f) { g_where -= 1.0f; g_seg++; }
    if (g_seg >= segments) { g_seg = 0; reconfigure(); }

    xyz[0] = interpolate(path[g_seg][0], path[g_seg][3], path[g_seg+1][0], path[g_seg+1][3], g_where);
    xyz[1] = interpolate(path[g_seg][1], path[g_seg][4], path[g_seg+1][1], path[g_seg+1][4], g_where);
    xyz[2] = interpolate(path[g_seg][2], path[g_seg][5], path[g_seg+1][2], path[g_seg+1][5], g_where);

    dir = xyz - g_oldxyz;
    dir.normalize();
    angvel.cross(dir, g_oldDir);
    float dot = g_oldDir.dot(dir);
    if (dot < -1.0f) dot = -1.0f;
    if (dot >  1.0f) dot =  1.0f;
    float angle = acosf(dot);
    const float maxSpin = 0.25f * (float)dSpeed * frameTime;
    if (angle >  maxSpin) angle =  maxSpin;
    if (angle < -maxSpin) angle = -maxSpin;
    angvel.scale(angle);

    tempVec = angvel - g_oldAngvel;
    float dist = tempVec.length();
    const float rotInertia = 0.007f * (float)dSpeed * frameTime;
    if (dist > rotInertia) {
        tempVec.scale(rotInertia / dist);
        angvel = g_oldAngvel + tempVec;
    }

    g_flymodeChange -= frameTime;
    if (g_flymodeChange <= 1.0f) angvel.scale(g_flymodeChange);
    if (g_flymodeChange <= 0.0f) {
        g_flymode = rsRandi(4);
        g_flymodeChange = rsRandf((float)(150 - dSpeed)) + 5.0f;
    }

    tempVec = angvel;
    angle = tempVec.normalize();
    newQuat.make(angle, tempVec[0], tempVec[1], tempVec[2]);
    if (g_flymode) g_quat.preMult(newQuat);
    else           g_quat.postMult(newQuat);

    /* Roll — direct assignment matches original; cap reduced ~37% to compensate
     * for the wider FOV making angular motion appear proportionally faster.
     * Acceleration is scaled to keep the same ~4-second ramp time as original. */
    const float rollCap = 0.025f * (float)dSpeed;  /* ← tune to adjust max roll speed */
    g_rollChange -= frameTime;
    if (g_rollChange <= 0.0f) {
        g_rollAcc    = rsRandf(0.5f * rollCap) - 0.25f * rollCap;  /* ~4s to cap at max */
        g_rollChange = rsRandf(10.0f) + 2.0f;
    }
    g_rollVel += g_rollAcc * frameTime;
    g_rollVel *= (1.0f - frameTime * 0.35f);  /* friction keeps roll from persisting forever */
    if (g_rollVel >  rollCap && g_rollAcc > 0.0f) g_rollAcc = 0.0f;  /* hard stop, original behaviour */
    if (g_rollVel < -rollCap && g_rollAcc < 0.0f) g_rollAcc = 0.0f;
    newQuat.make(g_rollVel * frameTime, g_oldDir[0], g_oldDir[1], g_oldDir[2]);
    g_quat.preMult(newQuat);

    g_quat.toMat(g_rotMat);

    g_oldxyz = xyz;
    g_oldDir[0] = -g_rotMat[2];
    g_oldDir[1] = -g_rotMat[6];
    g_oldDir[2] = -g_rotMat[10];
    angvel.scale(1.0f - frameTime * 0.1f);
    g_oldAngvel = angvel;

    g_xyz = xyz;
}

/* -------------------------------------------------------------------------
 * Shader build + uniform upload
 * ---------------------------------------------------------------------- */

static void build_shader_program() {
    static const char VERT_SRC[] =
        "#version 140\n"
        "in vec3 a_pos;\n"
        "in vec3 a_nrm;\n"
        "in vec2 a_tc;\n"
        "in vec4 a_col;\n"
        "in vec3 a_inst;\n"            /* per-instance world-space translation */
        "uniform mat4  u_proj;\n"
        "uniform mat4  u_mv;\n"
        "uniform int   u_lit;\n"
        "uniform float u_shin;\n"
        "uniform vec3  u_ldir;\n"      /* light direction in eye space, normalised */
        "uniform int   u_smap;\n"
        "uniform int   u_fog;\n"
        "uniform float u_fog0;\n"
        "uniform float u_fog1;\n"
        "out vec4  v_col;\n"
        "out vec2  v_tc;\n"
        "out float v_fog;\n"
        "void main() {\n"
        "    vec4 eye = u_mv * vec4(a_pos + a_inst, 1.0);\n"
        "    gl_Position = u_proj * eye;\n"
        "    vec3 n = normalize(mat3(u_mv) * a_nrm);\n"
        "    if (u_lit == 1) {\n"
        "        float d = max(dot(n, u_ldir), 0.0);\n"
        "        vec3  h = normalize(u_ldir + normalize(-eye.xyz));\n"
        "        float s = pow(max(dot(n, h), 0.0), max(u_shin, 1.0));\n"
        /* 0.2 = GL_LIGHT_MODEL_AMBIENT default; specular colour is white */
        "        v_col = vec4(a_col.rgb * (d + 0.2) + vec3(s), a_col.a);\n"
        "    } else {\n"
        "        v_col = a_col;\n"
        "    }\n"
        "    if (u_smap == 1) {\n"
        "        vec3  r = reflect(normalize(eye.xyz), n);\n"
        "        float m = 2.0 * sqrt(max(r.x*r.x + r.y*r.y + (r.z+1.0)*(r.z+1.0), 0.0001));\n"
        "        v_tc = vec2(r.x/m + 0.5, r.y/m + 0.5);\n"
        "    } else {\n"
        "        v_tc = a_tc;\n"
        "    }\n"
        "    float dep = abs(eye.z);\n"
        "    v_fog = (u_fog == 1) ? clamp((u_fog1-dep)/(u_fog1-u_fog0), 0.0, 1.0) : 1.0;\n"
        "}\n";

    static const char FRAG_SRC[] =
        "#version 140\n"
        "in vec4  v_col;\n"
        "in vec2  v_tc;\n"
        "in float v_fog;\n"
        /* 0=no tex  1=modulate(RGB)  2=decal(RGBA)  3=modulate alpha only (GL_ALPHA textures) */
        "uniform int       u_tex;\n"
        "uniform sampler2D u_samp;\n"
        "out vec4 frag;\n"
        "void main() {\n"
        "    vec4 c = v_col;\n"
        "    if (u_tex == 1) {\n"
        "        c *= texture(u_samp, v_tc);\n"
        "    } else if (u_tex == 2) {\n"
        "        vec4 t = texture(u_samp, v_tc);\n"
        "        c = vec4(mix(c.rgb, t.rgb, t.a), c.a);\n"
        "    } else if (u_tex == 3) {\n"
        /* GL_ALPHA textures return (0,0,0,A) via texture(); only modulate alpha */
        "        c.a *= texture(u_samp, v_tc).a;\n"
        "    }\n"
        "    frag = vec4(c.rgb * v_fog, c.a);\n"
        "}\n";

    auto compile = [](GLenum type, const char *src) -> GLuint {
        GLuint s = fn_CreateShader(type);
        fn_ShaderSource(s, 1, &src, NULL);
        fn_CompileShader(s);
        GLint ok; fn_GetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512]; fn_GetShaderInfoLog(s, 512, NULL, log);
            fprintf(stderr, "lattice shader: %s\n", log);
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
    fn_BindAttribLocation(g_prog, 1, "a_nrm");
    fn_BindAttribLocation(g_prog, 2, "a_tc");
    fn_BindAttribLocation(g_prog, 3, "a_col");
    fn_BindAttribLocation(g_prog, 4, "a_inst");
    fn_LinkProgram(g_prog);
    GLint ok; fn_GetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512]; fn_GetProgramInfoLog(g_prog, 512, NULL, log);
        fprintf(stderr, "lattice link: %s\n", log);
    }
    fn_DeleteShader(vs);
    fn_DeleteShader(fs);

    fn_UseProgram(g_prog);
    g_u_proj = fn_GetUniformLocation(g_prog, "u_proj");
    g_u_mv   = fn_GetUniformLocation(g_prog, "u_mv");
    g_u_lit  = fn_GetUniformLocation(g_prog, "u_lit");
    g_u_shin = fn_GetUniformLocation(g_prog, "u_shin");
    g_u_ldir = fn_GetUniformLocation(g_prog, "u_ldir");
    g_u_smap = fn_GetUniformLocation(g_prog, "u_smap");
    g_u_fog  = fn_GetUniformLocation(g_prog, "u_fog");
    g_u_fog0 = fn_GetUniformLocation(g_prog, "u_fog0");
    g_u_fog1 = fn_GetUniformLocation(g_prog, "u_fog1");
    g_u_tex  = fn_GetUniformLocation(g_prog, "u_tex");
    g_u_samp = fn_GetUniformLocation(g_prog, "u_samp");
    fn_Uniform1i(g_u_samp, 0);   /* always sample from texture unit 0 */
}

/* Upload preset-dependent shader uniforms.  Call after any preset change. */
static void set_shader_uniforms() {
    if (!g_prog) return;
    fn_UseProgram(g_prog);

    int lit = (dTexture == 3 || dTexture == 4 || dTexture == 6) ? 0 : 1;
    fn_Uniform1i(g_u_lit, lit);

    float shin = (dTexture == 2) ? 10.0f : (dTexture == 1) ? 1.0f : 50.0f;
    fn_Uniform1f(g_u_shin, shin);

    int smap = (dTexture==2||dTexture==3||dTexture==4||dTexture==5||dTexture==6) ? 1 : 0;
    fn_Uniform1i(g_u_smap, smap);

    fn_Uniform1i(g_u_fog,  dFog ? 1 : 0);
    fn_Uniform1f(g_u_fog0, (float)dDepth * 0.3f);
    fn_Uniform1f(g_u_fog1, (float)dDepth - 0.1f);

    /* 0=no texture  1=modulate(RGB)  2=decal(RGBA)  3=modulate alpha only (GL_ALPHA tex) */
    int tex = 0;
    if (dTexture == 5 || dTexture == 8) tex = 2;         /* RGBA decal: shiny, doughnuts */
    else if (dTexture == 6 || dTexture == 7) tex = 3;    /* alpha-only: ghostly, circuits */
    else if (dTexture != 0) tex = 1;                     /* RGB modulate: everything else */
    fn_Uniform1i(g_u_tex, tex);
}

/* -------------------------------------------------------------------------
 * Scene render  (uses whatever state update_physics last wrote)
 * ---------------------------------------------------------------------- */

static void render_scene() {
    /* Build MV matrix: rotation * T(-camera_pos) */
    float T[16], MV[16];
    mat4_translate(T, -g_xyz[0], -g_xyz[1], -g_xyz[2]);
    mat4_mul(MV, g_rotMat, T);
    fn_UniformMatrix4fv(g_u_proj, 1, GL_FALSE, g_proj_mat);
    fn_UniformMatrix4fv(g_u_mv,   1, GL_FALSE, MV);

    /* Transform directional light (world-space, w=0) into eye space via rotation only */
    static const float lw[3] = {400.0f, 300.0f, 500.0f};
    static const float llen  = 707.106781f;  /* sqrt(400^2+300^2+500^2) */
    float le[3] = {
        (g_rotMat[0]*lw[0] + g_rotMat[4]*lw[1] + g_rotMat[8] *lw[2]) / llen,
        (g_rotMat[1]*lw[0] + g_rotMat[5]*lw[1] + g_rotMat[9] *lw[2]) / llen,
        (g_rotMat[2]*lw[0] + g_rotMat[6]*lw[1] + g_rotMat[10]*lw[2]) / llen,
    };
    fn_Uniform3fv(g_u_ldir, 1, le);

    if (dTexture) glBindTexture(GL_TEXTURE_2D, texture_id[0]);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* --- Pass 1: collect visible cells (single loop, avoids duplicate frustum test) */
    struct CellRef { short i, j, k, t; };
    static CellRef vis[16000];
    int nvis = 0;

    float tv0 = (float)(globalxyz[0] - g_drawDepth) - g_xyz[0];
    int mod_i = myMod(globalxyz[0] - g_drawDepth);
    for (int i = globalxyz[0]-g_drawDepth; i <= globalxyz[0]+g_drawDepth; i++) {
        float part_i_0 = tv0 * g_rotMat[0];
        float part_i_1 = tv0 * g_rotMat[1];
        float part_i_2 = tv0 * g_rotMat[2];

        float tv1 = (float)(globalxyz[1] - g_drawDepth) - g_xyz[1];
        int mod_j = myMod(globalxyz[1] - g_drawDepth);
        for (int j = globalxyz[1]-g_drawDepth; j <= globalxyz[1]+g_drawDepth; j++) {
            float part_j_0 = part_i_0 + tv1 * g_rotMat[4];
            float part_j_1 = part_i_1 + tv1 * g_rotMat[5];
            float part_j_2 = part_i_2 + tv1 * g_rotMat[6];

            float tv2 = (float)(globalxyz[2] - g_drawDepth) - g_xyz[2];
            int mod_k = myMod(globalxyz[2] - g_drawDepth);
            for (int k = globalxyz[2]-g_drawDepth; k <= globalxyz[2]+g_drawDepth; k++) {
                float ep[3] = {
                    part_j_0 + tv2 * g_rotMat[8],
                    part_j_1 + tv2 * g_rotMat[9],
                    part_j_2 + tv2 * g_rotMat[10]
                };
                if (theCamera->inViewVolume(ep, 0.9f)) {
                    int t = (int)latticeGrid[mod_i][mod_j][mod_k];
                    if (g_obj_vcount[t] > 0 && nvis < 16000) {
                        vis[nvis++] = {(short)i,(short)j,(short)k,(short)t};
                    }
                }
                tv2 += 1.0f;
                mod_k++;
                if (mod_k >= LATSIZE) mod_k = 0;
            }
            tv1 += 1.0f;
            mod_j++;
            if (mod_j >= LATSIZE) mod_j = 0;
        }
        tv0 += 1.0f;
        mod_i++;
        if (mod_i >= LATSIZE) mod_i = 0;
    }
    if (nvis == 0) return;

    /* --- Pass 2: count instances per object type, then fill instance buffer */
    int type_cnt[NUMOBJECTS]   = {};
    for (int v = 0; v < nvis; v++) type_cnt[vis[v].t]++;

    int type_start[NUMOBJECTS+1];
    type_start[0] = 0;
    for (int t = 0; t < NUMOBJECTS; t++) type_start[t+1] = type_start[t] + type_cnt[t];
    int total = type_start[NUMOBJECTS];

    static short inst_data[16000][3];
    int fill[NUMOBJECTS] = {};
    for (int v = 0; v < nvis; v++) {
        int t   = vis[v].t;
        int idx = type_start[t] + fill[t]++;
        inst_data[idx][0] = vis[v].i;
        inst_data[idx][1] = vis[v].j;
        inst_data[idx][2] = vis[v].k;
    }

    /* --- Upload instance translations and set up vertex attributes */
    fn_BindBuffer(GL_ARRAY_BUFFER, g_inst_vbo);
    fn_BufferData(GL_ARRAY_BUFFER, total * sizeof(short) * 3, inst_data, GL_DYNAMIC_DRAW);

    /* Geometry attributes (attribs 0-3) from the static object VBO */
    fn_BindBuffer(GL_ARRAY_BUFFER, g_all_vbo);
    fn_EnableVertexAttribArray(0);
    fn_EnableVertexAttribArray(1);
    fn_EnableVertexAttribArray(2);
    fn_EnableVertexAttribArray(3);
    fn_VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 48, (const void *) 0);
    fn_VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 48, (const void *)12);
    fn_VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 48, (const void *)24);
    fn_VertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 48, (const void *)32);
    fn_VertexAttribDivisor(0, 0);
    fn_VertexAttribDivisor(1, 0);
    fn_VertexAttribDivisor(2, 0);
    fn_VertexAttribDivisor(3, 0);

    /* Per-instance attribute (attrib 4): advances once per instance */
    fn_BindBuffer(GL_ARRAY_BUFFER, g_inst_vbo);
    fn_EnableVertexAttribArray(4);
    fn_VertexAttribDivisor(4, 1);

    /* Bind IBO — stays bound for all instanced draw calls below */
    fn_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_all_ibo);

    /* --- One instanced draw per non-empty object type (~20 calls max) */
    for (int t = 0; t < NUMOBJECTS; t++) {
        if (type_cnt[t] == 0 || g_obj_icount[t] == 0) continue;
        fn_BindBuffer(GL_ARRAY_BUFFER, g_inst_vbo);
        fn_VertexAttribPointer(4, 3, GL_SHORT, GL_FALSE, sizeof(short) * 3,
                               (const void *)(ptrdiff_t)(type_start[t] * sizeof(short) * 3));
        fn_DrawElementsInstanced(GL_TRIANGLES, g_obj_icount[t], GL_UNSIGNED_INT,
                                 (const void *)(ptrdiff_t)(g_obj_istart[t] * (int)sizeof(unsigned int)),
                                 type_cnt[t]);
    }

    fn_DisableVertexAttribArray(0);
    fn_DisableVertexAttribArray(1);
    fn_DisableVertexAttribArray(2);
    fn_DisableVertexAttribArray(3);
    fn_DisableVertexAttribArray(4);
    fn_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    fn_BindBuffer(GL_ARRAY_BUFFER, 0);
}

/* -------------------------------------------------------------------------
 * Texture initialisation — faithful port of original initTextures()
 * ---------------------------------------------------------------------- */

static void initTextures() {
    glGenTextures(2, texture_id);

    auto setup = [](GLenum env, GLenum min = GL_LINEAR_MIPMAP_LINEAR) {
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, env);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min);
    };

    switch (dTexture) {
    case 1:
        glBindTexture(GL_TEXTURE_2D, texture_id[0]);
        setup(GL_MODULATE);
        gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, TEXSIZE, TEXSIZE,
                          GL_RGB, GL_UNSIGNED_BYTE, indtex1);
        glBindTexture(GL_TEXTURE_2D, texture_id[1]);
        setup(GL_MODULATE);
        gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, TEXSIZE, TEXSIZE,
                          GL_RGB, GL_UNSIGNED_BYTE, indtex2);
        break;
    case 2:
        glBindTexture(GL_TEXTURE_2D, texture_id[0]);
        setup(GL_MODULATE);
        gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, TEXSIZE, TEXSIZE,
                          GL_RGB, GL_UNSIGNED_BYTE, crystex);
        break;
    case 3:
        glBindTexture(GL_TEXTURE_2D, texture_id[0]);
        setup(GL_MODULATE);
        gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, TEXSIZE, TEXSIZE,
                          GL_RGB, GL_UNSIGNED_BYTE, chrometex);
        break;
    case 4:
        glBindTexture(GL_TEXTURE_2D, texture_id[0]);
        setup(GL_MODULATE);
        gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, TEXSIZE, TEXSIZE,
                          GL_RGB, GL_UNSIGNED_BYTE, brasstex);
        break;
    case 5:
        glBindTexture(GL_TEXTURE_2D, texture_id[0]);
        setup(GL_DECAL);
        gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, TEXSIZE, TEXSIZE,
                          GL_RGBA, GL_UNSIGNED_BYTE, shinytex);
        break;
    case 6:
        glBindTexture(GL_TEXTURE_2D, texture_id[0]);
        setup(GL_MODULATE);
        gluBuild2DMipmaps(GL_TEXTURE_2D, GL_ALPHA, TEXSIZE, TEXSIZE,
                          GL_ALPHA, GL_UNSIGNED_BYTE, ghostlytex);
        break;
    case 7:
        glBindTexture(GL_TEXTURE_2D, texture_id[0]);
        setup(GL_MODULATE);
        gluBuild2DMipmaps(GL_TEXTURE_2D, GL_ALPHA, TEXSIZE, TEXSIZE,
                          GL_ALPHA, GL_UNSIGNED_BYTE, circuittex);
        break;
    case 8:
        glBindTexture(GL_TEXTURE_2D, texture_id[0]);
        setup(GL_DECAL);
        gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, TEXSIZE, TEXSIZE,
                          GL_RGBA, GL_UNSIGNED_BYTE, doughtex);
        break;
    }
}

/* -------------------------------------------------------------------------
 * One-time GL initialization
 * ---------------------------------------------------------------------- */

static void initSaver() {
    int i, j, k;
    srand((unsigned)time(NULL));

    /* Depth test: disabled for crystal (2) and ghostly (6) — original behaviour */
    if (dTexture != 2 && dTexture != 6)
        glEnable(GL_DEPTH_TEST);
    glFrontFace(GL_CCW);
    glEnable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    setupProjection(g_win_width, g_win_height);

    /* Lighting: disabled for chrome (3), brass (4), ghostly (6) */
    if (dTexture != 3 && dTexture != 4 && dTexture != 6) {
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);
        float ambient[]  = {0.0f, 0.0f, 0.0f, 0.0f};
        float diffuse[]  = {1.0f, 1.0f, 1.0f, 0.0f};
        float specular[] = {1.0f, 1.0f, 1.0f, 0.0f};
        float position[] = {400.0f, 300.0f, 500.0f, 0.0f};
        glLightfv(GL_LIGHT0, GL_AMBIENT,  ambient);
        glLightfv(GL_LIGHT0, GL_DIFFUSE,  diffuse);
        glLightfv(GL_LIGHT0, GL_SPECULAR, specular);
        glLightfv(GL_LIGHT0, GL_POSITION, position);
    }
    glEnable(GL_COLOR_MATERIAL);
    if (dTexture == 0 || dTexture == 5 || dTexture >= 7) {
        glMaterialf(GL_FRONT, GL_SHININESS, 50.0f);
        glColorMaterial(GL_FRONT, GL_SPECULAR);  /* briefly track specular → sets white */
    }
    if (dTexture == 2) {
        glMaterialf(GL_FRONT, GL_SHININESS, 10.0f);
        glColorMaterial(GL_FRONT, GL_SPECULAR);
    }
    glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);

    /* Alpha blending: crystal (2) and ghostly (6) additive; circuits (7) alpha-blend */
    if (dTexture == 2 || dTexture == 6) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glEnable(GL_BLEND);
    }
    if (dTexture == 7) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_BLEND);
    }

    if (dFog) {
        glEnable(GL_FOG);
        float fog_color[] = {0.0f, 0.0f, 0.0f, 0.0f};
        glFogfv(GL_FOG_COLOR, fog_color);
        glFogf(GL_FOG_MODE, GL_LINEAR);
        glFogf(GL_FOG_START, (float)dDepth * 0.3f);
        glFogf(GL_FOG_END,   (float)dDepth - 0.1f);
    }

    if (dTexture) {
        glEnable(GL_TEXTURE_2D);
        initTextures();
    }

    buildLatticeVBOs();
    for (i = 0; i < LATSIZE; i++)
        for (j = 0; j < LATSIZE; j++)
            for (k = 0; k < LATSIZE; k++)
                latticeGrid[i][j][k] = (unsigned int)rsRandi(NUMOBJECTS);

    build_shader_program();
    fn_GenBuffers(1, &g_inst_vbo);
    set_shader_uniforms();

    /* Border points */
    for (i = 0; i < 10; i++) for (j = 0; j < 6; j++) bPnt[i][j] = 0.0f;
    bPnt[0][0]= 0.5f; bPnt[0][1]=-0.25f; bPnt[0][2]= 0.25f; bPnt[0][3]=1.0f;
    bPnt[1][0]= 0.5f; bPnt[1][1]= 0.25f; bPnt[1][2]=-0.25f; bPnt[1][3]=1.0f;
    bPnt[2][0]=-0.25f;bPnt[2][1]= 0.5f;  bPnt[2][2]= 0.25f; bPnt[2][4]=1.0f;
    bPnt[3][0]= 0.25f;bPnt[3][1]= 0.5f;  bPnt[3][2]=-0.25f; bPnt[3][4]=1.0f;
    bPnt[4][0]=-0.25f;bPnt[4][1]=-0.25f; bPnt[4][2]= 0.5f;  bPnt[4][5]=1.0f;
    bPnt[5][0]= 0.25f;bPnt[5][1]= 0.25f; bPnt[5][2]= 0.5f;  bPnt[5][5]=1.0f;
    bPnt[6][0]=0.5f; bPnt[6][1]=-0.5f; bPnt[6][2]=-0.5f; bPnt[6][3]=1.0f; bPnt[6][4]=-1.0f; bPnt[6][5]=-1.0f;
    bPnt[7][0]=0.5f; bPnt[7][1]= 0.5f; bPnt[7][2]=-0.5f; bPnt[7][3]=1.0f; bPnt[7][4]= 1.0f; bPnt[7][5]=-1.0f;
    bPnt[8][0]=0.5f; bPnt[8][1]=-0.5f; bPnt[8][2]= 0.5f; bPnt[8][3]=1.0f; bPnt[8][4]=-1.0f; bPnt[8][5]= 1.0f;
    bPnt[9][0]=0.5f; bPnt[9][1]= 0.5f; bPnt[9][2]= 0.5f; bPnt[9][3]=1.0f; bPnt[9][4]= 1.0f; bPnt[9][5]= 1.0f;

    globalxyz[0] = globalxyz[1] = globalxyz[2] = 0;

    path[0][0]=path[0][1]=path[0][2]=0.0f;
    path[0][3]=path[0][4]=path[0][5]=0.0f;
    j = rsRandi(12);
    k = j % 6;
    for (i = 0; i < 6; i++) path[1][i] = bPnt[k][i];
    if (j > 5) { i = k/2; path[1][i]*=-1.0f; path[1][i+3]*=-1.0f; }
    lastBorder = k;
    segments = 1;

    /* Physics state init */
    g_oldDir     = rsVec(0.0f, 0.0f, -1.0f);
    g_rollChange = rsRandf(10.0f) + 2.0f;   /* random 2-12s before first roll, matches original */
    g_drawDepth  = dDepth + 2;
}

static void render_frame();

/* -------------------------------------------------------------------------
 * XDG-shell callbacks
 * ---------------------------------------------------------------------- */

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
        setupProjection(g_win_width, g_win_height);
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

/* -------------------------------------------------------------------------
 * Runtime preset switching — tears down GL visual state and rebuilds it
 * without resetting the camera or path so the transition is seamless.
 * ---------------------------------------------------------------------- */

static void applyPreset(int idx) {
    const Preset &P = PRESETS[idx];
    dLongitude = P.longitude;  dLatitude = P.latitude;
    dThick     = P.thick;      dDensity  = P.density;
    dDepth     = P.depth;      dFov      = P.fov;
    dPathrand  = P.pathrand;   dSpeed    = P.speed;
    dSmooth    = P.smooth;     dFog      = P.fog;
    dTexture   = P.texType;    dPresetName = P.name;

    /* Tear down current visual GL state */
    glDisable(GL_LIGHTING);
    glDisable(GL_BLEND);
    glDisable(GL_FOG);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDeleteTextures(2, texture_id);
    fn_DeleteBuffers(1, &g_all_vbo);
    fn_DeleteBuffers(1, &g_all_ibo);

    /* Rebuild for new preset */
    if (dTexture != 2 && dTexture != 6) glEnable(GL_DEPTH_TEST);

    if (dTexture != 3 && dTexture != 4 && dTexture != 6) {
        glEnable(GL_LIGHTING); glEnable(GL_LIGHT0);
        glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);
        float ambient[]  = {0.0f, 0.0f, 0.0f, 0.0f};
        float diffuse[]  = {1.0f, 1.0f, 1.0f, 0.0f};
        float specular[] = {1.0f, 1.0f, 1.0f, 0.0f};
        float position[] = {400.0f, 300.0f, 500.0f, 0.0f};
        glLightfv(GL_LIGHT0, GL_AMBIENT,  ambient);
        glLightfv(GL_LIGHT0, GL_DIFFUSE,  diffuse);
        glLightfv(GL_LIGHT0, GL_SPECULAR, specular);
        glLightfv(GL_LIGHT0, GL_POSITION, position);
    }
    glEnable(GL_COLOR_MATERIAL);
    if (dTexture == 0 || dTexture == 5 || dTexture >= 7) {
        glMaterialf(GL_FRONT, GL_SHININESS, 50.0f);
        glColorMaterial(GL_FRONT, GL_SPECULAR);
    }
    if (dTexture == 2) {
        glMaterialf(GL_FRONT, GL_SHININESS, 10.0f);
        glColorMaterial(GL_FRONT, GL_SPECULAR);
    }
    glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);

    if (dTexture == 2 || dTexture == 6) { glBlendFunc(GL_SRC_ALPHA, GL_ONE); glEnable(GL_BLEND); }
    if (dTexture == 7) { glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_BLEND); }

    if (dFog) {
        glEnable(GL_FOG);
        float fog_color[] = {0.0f, 0.0f, 0.0f, 0.0f};
        glFogfv(GL_FOG_COLOR, fog_color);
        glFogf(GL_FOG_MODE, GL_LINEAR);
        glFogf(GL_FOG_START, (float)dDepth * 0.3f);
        glFogf(GL_FOG_END,   (float)dDepth - 0.1f);
    }
    if (dTexture) { glEnable(GL_TEXTURE_2D); initTextures(); }
    buildLatticeVBOs();
    for (int i = 0; i < LATSIZE; i++)
        for (int j = 0; j < LATSIZE; j++)
            for (int k = 0; k < LATSIZE; k++)
                latticeGrid[i][j][k] = (unsigned int)rsRandi(NUMOBJECTS);

    set_shader_uniforms();
    g_drawDepth = dDepth + 2;

    char title[128];
    snprintf(title, sizeof(title), "Lattice — %s  (← → to cycle)", dPresetName);
    xdg_toplevel_set_title(g_toplevel, title);
}

/* -------------------------------------------------------------------------
 * Keyboard callbacks
 * ---------------------------------------------------------------------- */

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
            xdg_toplevel_set_fullscreen(g_toplevel, NULL);
    } else if (key == 105 || key == 106) {  /* KEY_LEFT / KEY_RIGHT */
        int n = 0; while (PRESETS[n].name) n++;
        g_preset_idx = (g_preset_idx + (key == 106 ? 1 : n - 1)) % n;
        applyPreset(g_preset_idx);
    }
}
static void kb_modifiers(void *d, struct wl_keyboard *kb, uint32_t ser,
                          uint32_t dep, uint32_t lat, uint32_t lock, uint32_t grp) {}
static void kb_repeat_info(void *d, struct wl_keyboard *kb,
                            int32_t rate, int32_t delay) {}
static const struct wl_keyboard_listener keyboard_lst = {
    kb_keymap, kb_enter, kb_leave, kb_key, kb_modifiers, kb_repeat_info
};

/* -------------------------------------------------------------------------
 * Seat callback
 * ---------------------------------------------------------------------- */

static void seat_capabilities(void *d, struct wl_seat *seat, uint32_t caps) {
    if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
        g_keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(g_keyboard, &keyboard_lst, NULL);
    }
}
static void seat_name(void *d, struct wl_seat *seat, const char *name) {}
static const struct wl_seat_listener seat_lst = { seat_capabilities, seat_name };

/* -------------------------------------------------------------------------
 * Registry callbacks
 * ---------------------------------------------------------------------- */

static void registry_global(void *d, struct wl_registry *reg,
                              uint32_t name, const char *iface, uint32_t ver)
{
    if (!strcmp(iface, wl_compositor_interface.name))
        g_compositor = (struct wl_compositor *)
            wl_registry_bind(reg, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, xdg_wm_base_interface.name)) {
        g_wm_base = (struct xdg_wm_base *)
            wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(g_wm_base, &wm_base_listener, NULL);
    } else if (!strcmp(iface, wl_seat_interface.name)) {
        g_seat = (struct wl_seat *)
            wl_registry_bind(reg, name, &wl_seat_interface, 5);
        wl_seat_add_listener(g_seat, &seat_lst, NULL);
    } else if (!strcmp(iface, zxdg_decoration_manager_v1_interface.name)) {
        g_deco_manager = (struct zxdg_decoration_manager_v1 *)
            wl_registry_bind(reg, name, &zxdg_decoration_manager_v1_interface, 1);
    }
}

/* -------------------------------------------------------------------------
 * Decoration callback — accept whatever mode the compositor gives us
 * ---------------------------------------------------------------------- */

static void deco_configure(void *data, struct zxdg_toplevel_decoration_v1 *deco,
                            uint32_t mode) {}
static const struct zxdg_toplevel_decoration_v1_listener deco_listener = {
    deco_configure
};
static void registry_remove(void *d, struct wl_registry *r, uint32_t n) {}
static const struct wl_registry_listener registry_lst = {
    registry_global, registry_remove
};

/* -------------------------------------------------------------------------
 * EGL initialization
 * ---------------------------------------------------------------------- */

static int init_egl() {
    EGLint major, minor;

    g_egl_display = eglGetDisplay((EGLNativeDisplayType)g_display);
    if (g_egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "lattice: eglGetDisplay failed\n");
        return 0;
    }
    if (!eglInitialize(g_egl_display, &major, &minor)) {
        fprintf(stderr, "lattice: eglInitialize failed\n");
        return 0;
    }

    if (!eglBindAPI(EGL_OPENGL_API)) {
        fprintf(stderr, "lattice: eglBindAPI(EGL_OPENGL_API) failed\n");
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
        /* Try without 24-bit depth */
        EGLint fallback[] = {
            EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
            EGL_NONE
        };
        if (!eglChooseConfig(g_egl_display, fallback, &g_egl_config, 1, &n) || n == 0) {
            fprintf(stderr, "lattice: no suitable EGL config\n");
            return 0;
        }
    }

    EGLint ctx_attribs[] = { EGL_NONE };
    g_egl_context = eglCreateContext(g_egl_display, g_egl_config,
                                      EGL_NO_CONTEXT, ctx_attribs);
    if (g_egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "lattice: eglCreateContext failed (0x%x)\n", eglGetError());
        return 0;
    }
    return 1;
}

static int create_egl_surface() {
    g_egl_window = wl_egl_window_create(g_surface, g_win_width, g_win_height);
    if (!g_egl_window) {
        fprintf(stderr, "lattice: wl_egl_window_create failed\n");
        return 0;
    }
    g_egl_surface = eglCreateWindowSurface(g_egl_display, g_egl_config,
                                            (EGLNativeWindowType)g_egl_window, NULL);
    if (g_egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "lattice: eglCreateWindowSurface failed (0x%x)\n", eglGetError());
        return 0;
    }
    if (!eglMakeCurrent(g_egl_display, g_egl_surface, g_egl_surface, g_egl_context)) {
        fprintf(stderr, "lattice: eglMakeCurrent failed\n");
        return 0;
    }
    eglSwapInterval(g_egl_display, 0);  /* frame callbacks own timing; no EGL blocking needed */
    return 1;
}

/* -------------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * Frame-callback-driven render loop (Wayland convention)
 *
 * render_frame() runs the physics accumulator then draws.  It registers the
 * next wl_surface_frame callback before eglSwapBuffers so the compositor
 * fires frame_done exactly once per presented frame.  The main loop is just
 * wl_display_dispatch() — no busy-polling, no Sleep, no manual vsync wait.
 * ---------------------------------------------------------------------- */

static void frame_done(void *, struct wl_callback *, uint32_t);  /* forward */
static const struct wl_callback_listener frame_listener = { frame_done };

/* FPS telemetry — printed to stderr once per second */
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
    /* Accumulate real elapsed time, then step physics at a fixed 60 fps rate.
     * On a 120 Hz display we render twice as often but physics still advance
     * at the same pace as the original screensaver on a 60 Hz machine. */
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    float real_dt = (float)g_wall_timer.tick();
    g_sim_accum += real_dt;
    if (g_sim_accum > 4.0f * SIM_DT)   /* cap: recover from pauses without lurching */
        g_sim_accum = 4.0f * SIM_DT;
    int steps = 0;
    while (g_sim_accum >= SIM_DT) {
        frameTime = SIM_DT;
        update_physics();
        g_sim_accum -= SIM_DT;
        ++steps;
    }

    render_scene();

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double frame_ms = ((t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9) * 1000.0;

    g_perf.frames++;
    g_perf.phys_steps  += steps;
    g_perf.frame_ms_sum += frame_ms;

    /* Report once per second */
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

    /* Attach next frame callback before swap so it is included in the commit */
    if (!g_benchmark_mode) {
        if (g_frame_callback) {
            wl_callback_destroy(g_frame_callback);
        }
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
        setupProjection(g_win_width, g_win_height);
    }

    render_frame();
}

int main(int argc, char *argv[]) {
    /* Apply preset first, then allow per-parameter overrides */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--preset") && i+1 < argc) {
            const char *pname = argv[++i];
            bool found = false;
            for (int p = 0; PRESETS[p].name; p++) {
                if (!strcmp(pname, PRESETS[p].name)) {
                    const Preset &P = PRESETS[p];
                    dLongitude  = P.longitude;  dLatitude  = P.latitude;
                    dThick      = P.thick;       dDensity   = P.density;
                    dDepth      = P.depth;       dFov       = P.fov;
                    dPathrand   = P.pathrand;    dSpeed     = P.speed;
                    dSmooth     = P.smooth;      dFog       = P.fog;
                    dTexture    = P.texType;
                    dPresetName = P.name;
                    g_preset_idx = p;
                    found = true; break;
                }
            }
            if (!found) {
                fprintf(stderr, "lattice: unknown preset '%s'\n"
                        "  available: regular chainmail brassmesh computer slick tasty\n", pname);
                return 1;
            }
        }
        else if (!strcmp(argv[i],"--speed")     && i+1<argc) dSpeed     = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--depth")     && i+1<argc) dDepth     = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--density")   && i+1<argc) dDensity   = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--thick")     && i+1<argc) dThick     = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--fov")       && i+1<argc) dFov       = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--longitude") && i+1<argc) dLongitude = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--latitude")  && i+1<argc) dLatitude  = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--pathrand")  && i+1<argc) dPathrand  = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--smooth"))                 dSmooth    = true;
        else if (!strcmp(argv[i],"--no-smooth"))              dSmooth    = false;
        else if (!strcmp(argv[i],"--fog"))                    dFog       = true;
        else if (!strcmp(argv[i],"--no-fog"))                 dFog       = false;
        else if (!strcmp(argv[i],"--fullscreen") || !strcmp(argv[i],"-fullscreen")) {
            g_start_fullscreen = 1;
        }
        else if (!strcmp(argv[i],"--benchmark") || !strcmp(argv[i],"-benchmark")) {
            g_benchmark_mode = true;
        }
        else {
            fprintf(stderr,
                "Usage: %s [--preset NAME] [--speed N] [--depth N] [--density N]\n"
                "          [--thick N] [--fov N] [--longitude N] [--latitude N]\n"
                "          [--pathrand N] [--smooth|--no-smooth] [--fog|--no-fog]\n"
                "          [--fullscreen|-fullscreen] [--benchmark|-benchmark]\n"
                "Presets: regular  chainmail  brassmesh  computer  slick  tasty\n",
                argv[0]);
            return 1;
        }
    }

    /* --- Wayland connection --- */
    g_display = wl_display_connect(NULL);
    if (!g_display) {
        fprintf(stderr, "lattice: cannot connect to Wayland display\n");
        return 1;
    }

    struct wl_registry *registry = wl_display_get_registry(g_display);
    wl_registry_add_listener(registry, &registry_lst, NULL);
    wl_display_roundtrip(g_display);
    fprintf(stderr, "DEBUG: Registry roundtrip done\n");

    if (!g_compositor || !g_wm_base) {
        fprintf(stderr, "lattice: missing required Wayland globals\n");
        return 1;
    }

    /* --- Create surface + xdg window --- */
    g_surface    = wl_compositor_create_surface(g_compositor);
    g_xdg_surface = xdg_wm_base_get_xdg_surface(g_wm_base, g_surface);
    xdg_surface_add_listener(g_xdg_surface, &xdg_surface_lst, NULL);
    g_toplevel = xdg_surface_get_toplevel(g_xdg_surface);
    xdg_toplevel_add_listener(g_toplevel, &toplevel_lst, NULL);
    char title[128];
    snprintf(title, sizeof(title), "Lattice — %s  (← → cycle, F fullscreen)", dPresetName);
    xdg_toplevel_set_title(g_toplevel, title);
    xdg_toplevel_set_app_id(g_toplevel, "lattice");

    /* Request server-side decorations (title bar + close button) */
    if (g_deco_manager) {
        g_toplevel_deco = zxdg_decoration_manager_v1_get_toplevel_decoration(
            g_deco_manager, g_toplevel);
        zxdg_toplevel_decoration_v1_add_listener(g_toplevel_deco, &deco_listener, NULL);
        zxdg_toplevel_decoration_v1_set_mode(g_toplevel_deco,
            ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    if (g_start_fullscreen) {
        xdg_toplevel_set_fullscreen(g_toplevel, NULL);
    }

    wl_surface_commit(g_surface);

    /* --- EGL context (before configure so we can create the surface after) --- */
    if (!init_egl()) return 1;

    /* --- Wait for initial configure --- */
    fprintf(stderr, "DEBUG: Waiting for initial configure...\n");
    while (!g_configured)
        wl_display_dispatch(g_display);
    fprintf(stderr, "DEBUG: Initial configure done (width: %d, height: %d)\n", g_win_width, g_win_height);

    if (g_needs_resize) {
        g_win_width  = g_new_width;
        g_win_height = g_new_height;
        g_needs_resize = 0;
    }

    /* --- Initialize OpenGL state + screensaver --- */
    fprintf(stderr, "DEBUG: Initializing EGL surface...\n");
    if (!create_egl_surface()) return 1;

    fprintf(stderr, "DEBUG: Loading functions...\n");
    load_vbo_fns();
    load_shader_fns();
    fprintf(stderr, "DEBUG: Initializing saver...\n");
    initSaver();

    /* --- Frame-callback render loop --- */
    fprintf(stderr, "DEBUG: Initializing performance metrics...\n");
    perf_init();
    g_wall_timer.tick();   /* prime: discard time elapsed during startup */
    
    if (g_benchmark_mode) {
        fprintf(stderr, "DEBUG: Entering benchmark loop...\n");
        while (g_running) {
            wl_display_dispatch_pending(g_display);
            render_frame();
        }
    } else {
        fprintf(stderr, "DEBUG: Calling first render_frame...\n");
        render_frame();        /* first frame; attaches callback chain to compositor */

        fprintf(stderr, "DEBUG: Entering main loop...\n");
        while (g_running) {
            if (wl_display_dispatch(g_display) < 0) break;
        }
    }

    /* --- Cleanup --- */
    if (g_frame_callback) {
        wl_callback_destroy(g_frame_callback);
        g_frame_callback = NULL;
    }
    if (g_prog)     fn_DeleteProgram(g_prog);
    if (g_inst_vbo) fn_DeleteBuffers(1, &g_inst_vbo);
    fn_DeleteBuffers(1, &g_all_vbo);
    fn_DeleteBuffers(1, &g_all_ibo);
    delete theCamera;
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
