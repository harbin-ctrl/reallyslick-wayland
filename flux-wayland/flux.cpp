
/*
 * Flux screensaver - Wayland/EGL port
 * Original Copyright (C) 2002 Terence M. Welsh (GPL-2.0+)
 * Ported to Linux by Tugrul Galatali
 * Wayland port: native Wayland client with EGL/OpenGL
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <vector>
#include <unistd.h>
#include <time.h>
#include <getopt.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glext.h>

#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-client-protocol.h"

/* -------------------------------------------------------------------------
 * Math library
 * ---------------------------------------------------------------------- */

static inline float rsRandf(float max) {
    return ((float)rand() / (float)RAND_MAX) * max;
}
static inline int rsRandi(int max) {
    return (max > 0) ? rand() % max : 0;
}

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
 * Constants
 * ---------------------------------------------------------------------- */

#define PI      3.14159265359f
#define PIx2    6.28318530718f
#define DEG2RAD 0.0174532925f

#define NUMCONSTS 8
#define LIGHTSIZE 64

/* -------------------------------------------------------------------------
 * Globals & Options
 * ---------------------------------------------------------------------- */

static int dFluxes;
static int dParticles;
static int dTrail;
static int dGeometry;
static int dSize;
static int dComplexity;
static int dRandomize;
static int dExpansion;
static int dRotation;
static int dWind;
static int dInstability;
static int dBlur;
static int dSmart;

static void setDefaults(int which) {
    switch (which) {
    case 1: // Regular
        dFluxes = 1; dParticles = 20; dTrail = 40; dGeometry = 2;
        dSize = 15; dComplexity = 3; dRandomize = 0; dExpansion = 40;
        dRotation = 30; dWind = 20; dInstability = 20; dBlur = 0;
        break;
    case 2: // Hypnotic
        dFluxes = 2; dParticles = 10; dTrail = 40; dGeometry = 2;
        dSize = 15; dComplexity = 3; dRandomize = 80; dExpansion = 20;
        dRotation = 0; dWind = 40; dInstability = 10; dBlur = 30;
        break;
    case 3: // Insane
        dFluxes = 4; dParticles = 30; dTrail = 8; dGeometry = 2;
        dSize = 25; dComplexity = 3; dRandomize = 0; dExpansion = 80;
        dRotation = 60; dWind = 40; dInstability = 100; dBlur = 10;
        break;
    case 4: // Sparklers
        dFluxes = 3; dParticles = 20; dTrail = 6; dGeometry = 1;
        dSize = 20; dComplexity = 3; dRandomize = 85; dExpansion = 60;
        dRotation = 30; dWind = 20; dInstability = 30; dBlur = 0;
        break;
    case 5: // Paradigm
        dFluxes = 1; dParticles = 40; dTrail = 40; dGeometry = 2;
        dSize = 5; dComplexity = 3; dRandomize = 90; dExpansion = 30;
        dRotation = 20; dWind = 10; dInstability = 5; dBlur = 10;
        break;
    case 6: // Galactic
        dFluxes = 1; dParticles = 2; dTrail = 1500; dGeometry = 2;
        dSize = 10; dComplexity = 3; dRandomize = 0; dExpansion = 5;
        dRotation = 25; dWind = 0; dInstability = 5; dBlur = 0;
        break;
    }
}

/* -------------------------------------------------------------------------
 * Wayland / EGL Globals
 * ---------------------------------------------------------------------- */
static struct wl_display *g_display;
static struct wl_compositor *g_compositor;
static struct xdg_wm_base *g_wm_base;
static struct wl_surface *g_surface;
static struct xdg_surface *g_xdg_surface;
static struct xdg_toplevel *g_toplevel;
static struct wl_seat *g_seat;
static struct wl_keyboard *g_keyboard;
static struct wl_egl_window *g_egl_window;
static struct zxdg_decoration_manager_v1 *g_deco_manager = NULL;
static struct zxdg_toplevel_decoration_v1 *g_toplevel_deco = NULL;

static EGLDisplay g_egl_display = EGL_NO_DISPLAY;
static EGLContext g_egl_context = EGL_NO_CONTEXT;
static EGLSurface g_egl_surface = EGL_NO_SURFACE;
static EGLConfig g_egl_config;

static int g_win_width = 800;
static int g_win_height = 600;
static int g_configured = 0;
static int g_running = 1;
static int g_needs_resize = 0;
static int g_new_width, g_new_height;
static int g_fullscreen = 0;
static struct wl_callback *g_frame_callback = NULL;

static const float SIM_DT = 1.0f / 60.0f;
static float g_sim_accum = 0.0f;

/* -------------------------------------------------------------------------
 * VBO & Shaders
 * ---------------------------------------------------------------------- */

struct GVertex {
    float pos[3];
    float normal[3];
    float tc[2];
};

struct PVertex {
    float pos[3];
    short nrm[4];
    float tc[2];
};

struct InstData {
    float pos[3];
    unsigned char col[4];
    float scale;
};

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER  0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW   0x88B4
#define GL_DYNAMIC_DRAW  0x88B8
#endif

typedef void (*PFN_GenBuffers_t)(GLsizei, GLuint *);
typedef void (*PFN_BindBuffer_t)(GLenum, GLuint);
typedef void (*PFN_BufferData_t)(GLenum, ptrdiff_t, const void *, GLenum);

static PFN_GenBuffers_t fn_GenBuffers = nullptr;
static PFN_BindBuffer_t fn_BindBuffer = nullptr;
static PFN_BufferData_t fn_BufferData = nullptr;

static PFNGLCREATESHADERPROC fn_CreateShader = nullptr;
static PFNGLSHADERSOURCEPROC fn_ShaderSource = nullptr;
static PFNGLCOMPILESHADERPROC fn_CompileShader = nullptr;
static PFNGLGETSHADERIVPROC fn_GetShaderiv = nullptr;
static PFNGLGETSHADERINFOLOGPROC fn_GetShaderInfoLog = nullptr;
static PFNGLCREATEPROGRAMPROC fn_CreateProgram = nullptr;
static PFNGLATTACHSHADERPROC fn_AttachShader = nullptr;
static PFNGLBINDATTRIBLOCATIONPROC fn_BindAttribLocation = nullptr;
static PFNGLLINKPROGRAMPROC fn_LinkProgram = nullptr;
static PFNGLGETPROGRAMIVPROC fn_GetProgramiv = nullptr;
static PFNGLGETPROGRAMINFOLOGPROC fn_GetProgramInfoLog = nullptr;
static PFNGLUSEPROGRAMPROC fn_UseProgram = nullptr;
static PFNGLGETUNIFORMLOCATIONPROC fn_GetUniformLocation = nullptr;
static PFNGLUNIFORM1IPROC fn_Uniform1i = nullptr;
static PFNGLUNIFORM3FVPROC fn_Uniform3fv = nullptr;
static PFNGLUNIFORMMATRIX4FVPROC fn_UniformMatrix4fv = nullptr;
static PFNGLENABLEVERTEXATTRIBARRAYPROC fn_EnableVertexAttribArray = nullptr;
static PFNGLDISABLEVERTEXATTRIBARRAYPROC fn_DisableVertexAttribArray = nullptr;
static PFNGLVERTEXATTRIBPOINTERPROC fn_VertexAttribPointer = nullptr;
static PFNGLVERTEXATTRIBDIVISORPROC fn_VertexAttribDivisor = nullptr;
static PFNGLDRAWARRAYSINSTANCEDPROC fn_DrawArraysInstanced = nullptr;
static PFNGLDRAWELEMENTSINSTANCEDPROC fn_DrawElementsInstanced = nullptr;
static PFNGLGENVERTEXARRAYSPROC fn_GenVertexArrays = nullptr;
static PFNGLBINDVERTEXARRAYPROC fn_BindVertexArray = nullptr;

static void load_gl_fns() {
    fn_GenBuffers = (PFN_GenBuffers_t)eglGetProcAddress("glGenBuffers");
    fn_BindBuffer = (PFN_BindBuffer_t)eglGetProcAddress("glBindBuffer");
    fn_BufferData = (PFN_BufferData_t)eglGetProcAddress("glBufferData");

    fn_CreateShader = (PFNGLCREATESHADERPROC)eglGetProcAddress("glCreateShader");
    fn_ShaderSource = (PFNGLSHADERSOURCEPROC)eglGetProcAddress("glShaderSource");
    fn_CompileShader = (PFNGLCOMPILESHADERPROC)eglGetProcAddress("glCompileShader");
    fn_GetShaderiv = (PFNGLGETSHADERIVPROC)eglGetProcAddress("glGetShaderiv");
    fn_GetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)eglGetProcAddress("glGetShaderInfoLog");
    fn_CreateProgram = (PFNGLCREATEPROGRAMPROC)eglGetProcAddress("glCreateProgram");
    fn_AttachShader = (PFNGLATTACHSHADERPROC)eglGetProcAddress("glAttachShader");
    fn_BindAttribLocation = (PFNGLBINDATTRIBLOCATIONPROC)eglGetProcAddress("glBindAttribLocation");
    fn_LinkProgram = (PFNGLLINKPROGRAMPROC)eglGetProcAddress("glLinkProgram");
    fn_GetProgramiv = (PFNGLGETPROGRAMIVPROC)eglGetProcAddress("glGetProgramiv");
    fn_GetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)eglGetProcAddress("glGetProgramInfoLog");
    fn_UseProgram = (PFNGLUSEPROGRAMPROC)eglGetProcAddress("glUseProgram");
    fn_GetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)eglGetProcAddress("glGetUniformLocation");
    fn_Uniform1i = (PFNGLUNIFORM1IPROC)eglGetProcAddress("glUniform1i");
    fn_Uniform3fv = (PFNGLUNIFORM3FVPROC)eglGetProcAddress("glUniform3fv");
    fn_UniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)eglGetProcAddress("glUniformMatrix4fv");
    fn_EnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)eglGetProcAddress("glEnableVertexAttribArray");
    fn_DisableVertexAttribArray = (PFNGLDISABLEVERTEXATTRIBARRAYPROC)eglGetProcAddress("glDisableVertexAttribArray");
    fn_VertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)eglGetProcAddress("glVertexAttribPointer");
    
    fn_VertexAttribDivisor = (PFNGLVERTEXATTRIBDIVISORPROC)eglGetProcAddress("glVertexAttribDivisor");
    if (!fn_VertexAttribDivisor) fn_VertexAttribDivisor = (PFNGLVERTEXATTRIBDIVISORPROC)eglGetProcAddress("glVertexAttribDivisorARB");
    
    fn_DrawArraysInstanced = (PFNGLDRAWARRAYSINSTANCEDPROC)eglGetProcAddress("glDrawArraysInstanced");
    if (!fn_DrawArraysInstanced) fn_DrawArraysInstanced = (PFNGLDRAWARRAYSINSTANCEDPROC)eglGetProcAddress("glDrawArraysInstancedARB");
    
    fn_DrawElementsInstanced = (PFNGLDRAWELEMENTSINSTANCEDPROC)eglGetProcAddress("glDrawElementsInstanced");
    if (!fn_DrawElementsInstanced) fn_DrawElementsInstanced = (PFNGLDRAWELEMENTSINSTANCEDPROC)eglGetProcAddress("glDrawElementsInstancedARB");

    fn_GenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)eglGetProcAddress("glGenVertexArrays");
    fn_BindVertexArray = (PFNGLBINDVERTEXARRAYPROC)eglGetProcAddress("glBindVertexArray");
}

static void mat4_identity(float M[16]) {
    memset(M, 0, 64); M[0]=M[5]=M[10]=M[15]=1.0f;
}
static void mat4_translate(float M[16], float tx, float ty, float tz) {
    mat4_identity(M); M[12]=tx; M[13]=ty; M[14]=tz;
}
static void mat4_rotate(float M[16], float deg, float ax, float ay, float az) {
    float a=deg*DEG2RAD, c=cosf(a), s=sinf(a), ic=1.0f-c;
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
 * Color Utils
 * ---------------------------------------------------------------------- */
static void hsl2rgb(float h, float s, float l, float &r, float &g, float &b) {
    float v = (l <= 0.5f) ? (l * (1.0f + s)) : (l + s - l * s);
    if (v <= 0.0f) { r = g = b = 0.0f; return; }
    float min = 2.0f * l - v;
    float sv = (v - min) / v;
    h = h * 6.0f;
    int sextant = (int)h;
    float fract = h - sextant;
    float vsf = v * sv * fract;
    float mid1 = min + vsf;
    float mid2 = v - vsf;
    switch (sextant) {
        case 0: r = v; g = mid1; b = min; break;
        case 1: r = mid2; g = v; b = min; break;
        case 2: r = min; g = v; b = mid1; break;
        case 3: r = min; g = mid2; b = v; break;
        case 4: r = mid1; g = min; b = v; break;
        case 5: r = v; g = min; b = mid2; break;
        default: r = v; g = mid1; b = min; break;
    }
}

/* -------------------------------------------------------------------------
 * Core Saver Logic
 * ---------------------------------------------------------------------- */
static int whichparticle = 0;
static float orbitiness = 0.0f;
static float prevOrbitiness = 0.0f;
static float lumdiff;

class particle {
public:
    float **vertices;
    int counter;
    float offset[3];

    particle() {
        offset[0] = cosf(PIx2 * float(whichparticle) / float(dParticles));
        offset[1] = float(whichparticle) / float(dParticles) - 0.5f;
        offset[2] = sinf(PIx2 * float(whichparticle) / float(dParticles));
        whichparticle++;

        vertices = new float*[dTrail];
        for (int i = 0; i < dTrail; i++) {
            vertices[i] = new float[5];
            vertices[i][0] = 0.0f;
            vertices[i][1] = 3.0f;
            vertices[i][2] = 0.0f;
            vertices[i][3] = 0.0f;
            vertices[i][4] = 0.0f;
        }
        counter = 0;
    }
    ~particle() {
        for (int i = 0; i < dTrail; i++) delete[] vertices[i];
        delete[] vertices;
    }
    float update(float *c) {
        float cx, cy, cz;
        float expander = 1.0f + 0.0005f * float(dExpansion);
        float blower = 0.001f * float(dWind);
        int oldc = counter;
        float oldpos[3];
        oldpos[0] = vertices[oldc][0];
        oldpos[1] = vertices[oldc][1];
        oldpos[2] = vertices[oldc][2];

        counter++;
        if (counter >= dTrail) counter = 0;

        cx = vertices[oldc][0] * (1.0f - 1.0f / (vertices[oldc][0] * vertices[oldc][0] + 1.0f));
        cy = vertices[oldc][1] * (1.0f - 1.0f / (vertices[oldc][1] * vertices[oldc][1] + 1.0f));
        cz = vertices[oldc][2] * (1.0f - 1.0f / (vertices[oldc][2] * vertices[oldc][2] + 1.0f));

        vertices[counter][0] = vertices[oldc][0] + c[6] * offset[0] - cx + c[2] * vertices[oldc][1] + c[5] * vertices[oldc][2];
        vertices[counter][1] = vertices[oldc][1] + c[6] * offset[1] - cy + c[1] * vertices[oldc][2] + c[4] * vertices[oldc][0];
        vertices[counter][2] = vertices[oldc][2] + c[6] * offset[2] - cz + c[0] * vertices[oldc][0] + c[3] * vertices[oldc][1];

        float xdiff = vertices[counter][0] - vertices[oldc][0];
        float ydiff = vertices[counter][1] - vertices[oldc][1];
        float zdiff = vertices[counter][2] - vertices[oldc][2];
        float distsq = vertices[counter][0] * vertices[counter][0] + vertices[counter][1] * vertices[counter][1] + vertices[counter][2] * vertices[counter][2];
        float oldDistsq = vertices[oldc][0] * vertices[oldc][0] + vertices[oldc][1] * vertices[oldc][1] + vertices[oldc][2] * vertices[oldc][2];
        orbitiness += (xdiff * xdiff + ydiff * ydiff + zdiff * zdiff) / (2.0f - fabs(distsq - oldDistsq));

        vertices[counter][3] = cx * cx + cy * cy + cz * cz;
        if (vertices[counter][3] > 1.0f) vertices[counter][3] = 1.0f;
        vertices[counter][3] += c[7];
        if (vertices[counter][3] > 1.0f) vertices[counter][3] -= 1.0f;
        if (vertices[counter][3] < 0.0f) vertices[counter][3] += 1.0f;
        
        vertices[counter][4] = c[0] + vertices[counter][3];
        if (vertices[counter][4] < 0.0f) vertices[counter][4] = -vertices[counter][4];
        vertices[counter][4] -= float(int(vertices[counter][4]));
        vertices[counter][4] = 1.0f - (vertices[counter][4] * vertices[counter][4]);

        if (!counter) {
            if (fabs(vertices[0][0]) > 10000.0f || fabs(vertices[0][1]) > 10000.0f || fabs(vertices[0][2]) > 10000.0f) {
                vertices[0][0] = rsRandf(2.0f) - 1.0f;
                vertices[0][1] = rsRandf(2.0f) - 1.0f;
                vertices[0][2] = rsRandf(2.0f) - 1.0f;
            }
        }
        
        int p = counter;
        for (int i = 0; i < dTrail; i++) {
            p++; if (p >= dTrail) p = 0;
            vertices[p][0] *= expander;
            vertices[p][1] *= expander;
            vertices[p][2] *= expander;
            vertices[p][2] += blower;
        }

        oldpos[0] -= vertices[counter][0];
        oldpos[1] -= vertices[counter][1];
        oldpos[2] -= vertices[counter][2];
        return sqrtf(oldpos[0]*oldpos[0] + oldpos[1]*oldpos[1] + oldpos[2]*oldpos[2]);
    }
};

class flux {
public:
    particle *particles;
    int randomize;
    float c[NUMCONSTS];
    float cv[NUMCONSTS];
    int currentSmartConstant;
    float oldDistance;

    flux() {
        particles = new particle[dParticles];
        randomize = 1;
        for (int i = 0; i < NUMCONSTS; i++) {
            c[i] = rsRandf(2.0f) - 1.0f;
            cv[i] = rsRandf(0.000005f * float(dInstability * dInstability)) + 0.000001f * float(dInstability * dInstability);
        }
        currentSmartConstant = 0;
        oldDistance = 0.0f;
    }
    ~flux() { delete[] particles; }
    void update() {
        if (dRandomize) {
            randomize--;
            if (randomize <= 0) {
                for (int i = 0; i < NUMCONSTS; i++) c[i] = rsRandf(2.0f) - 1.0f;
                int temp = 101 - dRandomize;
                temp = temp * temp;
                randomize = temp + rsRandi(temp);
            }
        }
        for (int i = 0; i < NUMCONSTS; i++) {
            c[i] += cv[i];
            if (c[i] >= 1.0f) { c[i] = 1.0f; cv[i] = -cv[i]; }
            if (c[i] <= -1.0f) { c[i] = -1.0f; cv[i] = -cv[i]; }
        }
        prevOrbitiness = orbitiness;
        orbitiness = 0.0f;

        float dist = 0.0f;
        for (int i = 0; i < dParticles; i++)
            dist = particles[i].update(c);

        dSmart = 0;
        if (dSmart) {
            const float upper = 0.4f, lower = 0.2f;
            int beSmart = 0;
            if (dist > upper && dist > oldDistance) beSmart = 1;
            if (dist < lower && dist < oldDistance) beSmart = 1;
            if (beSmart) {
                cv[currentSmartConstant] = -cv[currentSmartConstant];
                currentSmartConstant++;
                if (currentSmartConstant >= dSmart) currentSmartConstant = 0;
            }
            oldDistance = dist;
        }

        if (orbitiness < prevOrbitiness) {
            int i = rsRandi(NUMCONSTS - 1);
            cv[i] = -cv[i];
        }
    }
};

static flux *fluxes = nullptr;

static const char *g_preset_names[] = {
    "", "Regular", "Hypnotic", "Insane", "Sparklers", "Paradigm", "Galactic"
};
static int g_preset = 1;

// (Re)build the simulation for preset p (1..6), wrapping around at the ends.
// Presets change dParticles/dTrail/dFluxes, so the flux objects are recreated.
// The static geometry (sphere/quad/light texture) is preset-independent
// (dComplexity is constant across presets) and does not need rebuilding.
static void apply_preset(int p) {
    if (p < 1) p = 6;
    if (p > 6) p = 1;
    g_preset = p;
    // Free the old simulation BEFORE changing the globals: particle/flux
    // destructors read dTrail/dParticles, which must still match how the
    // existing objects were allocated (otherwise the frees run off the ends).
    delete[] fluxes;
    fluxes = nullptr;
    setDefaults(p);
    whichparticle = 0;
    lumdiff = 1.0f / float(dTrail);
    fluxes = new flux[dFluxes];
    g_sim_accum = 0.0f;
    fprintf(stderr, "[flux] preset %d: %s\n", p, g_preset_names[p]);
}

/* -------------------------------------------------------------------------
 * Rendering state
 * ---------------------------------------------------------------------- */
static GLuint g_prog = 0;
static GLint g_u_proj, g_u_mv, g_u_geom, g_u_lit, g_u_ldir, g_u_samp;
static GLuint g_vbo_sphere, g_ibo_sphere;
static GLuint g_vbo_quad, g_ibo_quad;
static GLuint g_inst_vbo;
static int g_sphere_indices = 0;
static int g_quad_indices = 0;
static float g_proj_mat[16];
static float g_cameraAngle = 0.0f;
static unsigned int g_tex;

static void build_shaders() {
    const char *vs_src = 
        "#version 140\n"
        "in vec3 a_pos;\n"
        "in vec3 a_nrm;\n"
        "in vec2 a_tc;\n"
        "in vec3 a_inst_pos;\n"
        "in vec4 a_inst_col;\n"
        "in float a_inst_scale;\n"
        "uniform mat4 u_proj;\n"
        "uniform mat4 u_mv;\n"
        "uniform int u_geom;\n"
        "uniform int u_lit;\n"
        "uniform vec3 u_ldir;\n"
        "out vec4 v_col;\n"
        "out vec2 v_tc;\n"
        "void main() {\n"
        "    vec3 wpos;\n"
        "    if (u_geom == 0) {\n"
        "        wpos = a_inst_pos;\n"
        "        gl_PointSize = max(a_inst_scale, 1.0);\n"
        "    } else {\n"
        "        wpos = a_pos * a_inst_scale + a_inst_pos;\n"
        "    }\n"
        "    vec4 eye = u_mv * vec4(wpos, 1.0);\n"
        "    gl_Position = u_proj * eye;\n"
        "    \n"
        "    if (u_lit == 1 && u_geom == 1) {\n"
        "        vec3 n = normalize(mat3(u_mv) * a_nrm);\n"
        "        float d = max(dot(n, u_ldir), 0.0);\n"
        "        vec3 h = normalize(u_ldir + normalize(-eye.xyz));\n"
        "        float s = pow(max(dot(n, h), 0.0), 50.0);\n"
        "        v_col = vec4(a_inst_col.rgb * (d + 0.2) + vec3(s), a_inst_col.a);\n"
        "    } else {\n"
        "        v_col = a_inst_col;\n"
        "    }\n"
        "    v_tc = a_tc;\n"
        "}\n";

    const char *fs_src = 
        "#version 140\n"
        "in vec4 v_col;\n"
        "in vec2 v_tc;\n"
        "uniform int u_geom;\n"
        "uniform sampler2D u_samp;\n"
        "out vec4 frag;\n"
        "void main() {\n"
        "    if (u_geom == 2) {\n"
        "        float lum = texture(u_samp, v_tc).r;\n"
        "        frag = vec4(v_col.rgb * lum, v_col.a);\n"
        "    } else if (u_geom == 0) {\n"
        "        vec2 p = gl_PointCoord * 2.0 - 1.0;\n"
        "        float r2 = dot(p, p);\n"
        "        if (r2 > 1.0) discard;\n"
        "        float lum = 1.0 - sqrt(r2);\n"
        "        lum = lum * lum;\n"
        "        frag = vec4(v_col.rgb * lum, v_col.a);\n"
        "    } else {\n"
        "        frag = v_col;\n"
        "    }\n"
        "}\n";

    auto compile = [](GLenum type, const char *src) {
        GLuint s = fn_CreateShader(type);
        fn_ShaderSource(s, 1, &src, nullptr);
        fn_CompileShader(s);
        GLint ok = 0;
        fn_GetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048];
            fn_GetShaderInfoLog(s, sizeof(log), nullptr, log);
            fprintf(stderr, "[flux] %s shader compile FAILED:\n%s\n",
                    type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        }
        return s;
    };
    GLuint vs = compile(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src);
    g_prog = fn_CreateProgram();
    fn_AttachShader(g_prog, vs);
    fn_AttachShader(g_prog, fs);
    fn_BindAttribLocation(g_prog, 0, "a_pos");
    fn_BindAttribLocation(g_prog, 1, "a_nrm");
    fn_BindAttribLocation(g_prog, 2, "a_tc");
    fn_BindAttribLocation(g_prog, 3, "a_inst_pos");
    fn_BindAttribLocation(g_prog, 4, "a_inst_col");
    fn_BindAttribLocation(g_prog, 5, "a_inst_scale");
    fn_LinkProgram(g_prog);
    GLint linked = 0;
    fn_GetProgramiv(g_prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[2048];
        fn_GetProgramInfoLog(g_prog, sizeof(log), nullptr, log);
        fprintf(stderr, "[flux] program link FAILED:\n%s\n", log);
    }
    fn_UseProgram(g_prog);
    
    g_u_proj = fn_GetUniformLocation(g_prog, "u_proj");
    g_u_mv = fn_GetUniformLocation(g_prog, "u_mv");
    g_u_geom = fn_GetUniformLocation(g_prog, "u_geom");
    g_u_lit = fn_GetUniformLocation(g_prog, "u_lit");
    g_u_ldir = fn_GetUniformLocation(g_prog, "u_ldir");
    g_u_samp = fn_GetUniformLocation(g_prog, "u_samp");
}

static void build_vbos() {
    // Generate Spheres
    int complexity = dComplexity + 1;
    int lat = complexity + 1;
    int lon = complexity + 2;
    std::vector<PVertex> sv;
    std::vector<unsigned short> si;
    
    for (int i = 0; i <= lat; i++) {
        float theta = i * PI / lat;
        float sinTheta = sinf(theta);
        float cosTheta = cosf(theta);
        for (int j = 0; j <= lon; j++) {
            float phi = j * PIx2 / lon;
            float sinPhi = sinf(phi);
            float cosPhi = cosf(phi);
            float x = cosPhi * sinTheta;
            float y = sinPhi * sinTheta;
            float z = cosTheta;
            PVertex v;
            v.pos[0] = x; v.pos[1] = y; v.pos[2] = z;
            v.nrm[0] = (short)(x * 32767); v.nrm[1] = (short)(y * 32767); v.nrm[2] = (short)(z * 32767); v.nrm[3] = 0;
            v.tc[0] = (float)j / lon; v.tc[1] = (float)i / lat;
            sv.push_back(v);
        }
    }
    for (int i = 0; i < lat; i++) {
        for (int j = 0; j < lon; j++) {
            int first = (i * (lon + 1)) + j;
            int second = first + lon + 1;
            si.push_back(first); si.push_back(second); si.push_back(first + 1);
            si.push_back(second); si.push_back(second + 1); si.push_back(first + 1);
        }
    }
    
    fn_GenBuffers(1, &g_vbo_sphere);
    fn_BindBuffer(GL_ARRAY_BUFFER, g_vbo_sphere);
    fn_BufferData(GL_ARRAY_BUFFER, sv.size() * sizeof(PVertex), sv.data(), GL_STATIC_DRAW);
    fn_GenBuffers(1, &g_ibo_sphere);
    fn_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo_sphere);
    fn_BufferData(GL_ELEMENT_ARRAY_BUFFER, si.size() * sizeof(unsigned short), si.data(), GL_STATIC_DRAW);
    g_sphere_indices = si.size();
    
    // Generate Quad
    PVertex qv[4] = {
        {{-1, -1, 0}, {0,0,32767,0}, {0, 0}},
        {{ 1, -1, 0}, {0,0,32767,0}, {1, 0}},
        {{ 1,  1, 0}, {0,0,32767,0}, {1, 1}},
        {{-1,  1, 0}, {0,0,32767,0}, {0, 1}}
    };
    unsigned short qi[6] = {0, 1, 2, 0, 2, 3};
    fn_GenBuffers(1, &g_vbo_quad);
    fn_BindBuffer(GL_ARRAY_BUFFER, g_vbo_quad);
    fn_BufferData(GL_ARRAY_BUFFER, sizeof(qv), qv, GL_STATIC_DRAW);
    fn_GenBuffers(1, &g_ibo_quad);
    fn_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo_quad);
    fn_BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(qi), qi, GL_STATIC_DRAW);
    g_quad_indices = 6;
    
    // Instance VBO
    fn_GenBuffers(1, &g_inst_vbo);
    
    // Light Texture
    unsigned char lightTexture[LIGHTSIZE][LIGHTSIZE];
    for (int i = 0; i < LIGHTSIZE; i++) {
        for (int j = 0; j < LIGHTSIZE; j++) {
            float x = float(i - LIGHTSIZE / 2) / float(LIGHTSIZE / 2);
            float y = float(j - LIGHTSIZE / 2) / float(LIGHTSIZE / 2);
            float temp = 1.0f - sqrtf(x * x + y * y);
            if (temp > 1.0f) temp = 1.0f;
            if (temp < 0.0f) temp = 0.0f;
            lightTexture[i][j] = (unsigned char)(255.0f * temp * temp);
        }
    }
    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    // GL_LUMINANCE is not a valid internal format in the GL 3.1 core profile;
    // use GL_R8 / GL_RED (the fragment shader samples the .r channel).
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, LIGHTSIZE, LIGHTSIZE, 0, GL_RED, GL_UNSIGNED_BYTE, lightTexture);
}

static void render_frame();

/* -------------------------------------------------------------------------
 * Wayland / EGL Boilerplate
 * ---------------------------------------------------------------------- */
static const struct wl_callback_listener frame_listener = {
    [](void *data, struct wl_callback *cb, uint32_t time) {
        wl_callback_destroy(cb);
        g_frame_callback = NULL;
        render_frame();
    }
};

static void configure_egl() {
    if (!eglBindAPI(EGL_OPENGL_API))
        fprintf(stderr, "[flux] eglBindAPI(OPENGL) failed: 0x%x\n", eglGetError());
    EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_NONE
    };
    EGLint num_config = 0;
    if (!eglChooseConfig(g_egl_display, attribs, &g_egl_config, 1, &num_config) || num_config == 0)
        fprintf(stderr, "[flux] eglChooseConfig failed/none: n=%d err=0x%x\n", num_config, eglGetError());
    EGLint ctx_attribs[] = { EGL_NONE };
    g_egl_context = eglCreateContext(g_egl_display, g_egl_config, EGL_NO_CONTEXT, ctx_attribs);
    if (g_egl_context == EGL_NO_CONTEXT)
        fprintf(stderr, "[flux] eglCreateContext failed: 0x%x\n", eglGetError());
    g_egl_surface = eglCreateWindowSurface(g_egl_display, g_egl_config, g_egl_window, NULL);
    if (g_egl_surface == EGL_NO_SURFACE)
        fprintf(stderr, "[flux] eglCreateWindowSurface failed: 0x%x\n", eglGetError());
    if (!eglMakeCurrent(g_egl_display, g_egl_surface, g_egl_surface, g_egl_context))
        fprintf(stderr, "[flux] eglMakeCurrent failed: 0x%x\n", eglGetError());
    eglSwapInterval(g_egl_display, 0);
    load_gl_fns();
    // GL 3.1 core has no default vertex array object; a VAO must be bound
    // before any glVertexAttribPointer / draw call, otherwise they fail with
    // GL_INVALID_OPERATION and nothing is rendered.
    if (fn_GenVertexArrays && fn_BindVertexArray) {
        GLuint vao = 0;
        fn_GenVertexArrays(1, &vao);
        fn_BindVertexArray(vao);
    }
    build_shaders();
    build_vbos();
}

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height, struct wl_array *states) {
    if (width > 0 && height > 0) {
        g_new_width = width;
        g_new_height = height;
        g_needs_resize = 1;
    }
    int is_fullscreen = 0;
    uint32_t *state;
    for (state = (uint32_t *)states->data;
         (const char *)state < ((const char *)states->data + states->size);
         state++) {
        if (*state == XDG_TOPLEVEL_STATE_FULLSCREEN) is_fullscreen = 1;
    }
    g_fullscreen = is_fullscreen;
}
static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel) { g_running = 0; }
static const struct xdg_toplevel_listener xdg_toplevel_listener = { xdg_toplevel_configure, xdg_toplevel_close };
static void xdg_surface_configure(void *data, struct xdg_surface *surface, uint32_t serial) {
    xdg_surface_ack_configure(surface, serial);
    if (!g_configured) {
        g_configured = 1;
        g_egl_window = wl_egl_window_create(g_surface, g_win_width, g_win_height);
        configure_egl();
        render_frame();
    } else if (g_needs_resize) {
        wl_egl_window_resize(g_egl_window, g_new_width, g_new_height, 0, 0);
        g_win_width = g_new_width;
        g_win_height = g_new_height;
        g_needs_resize = 0;
    }
}
static const struct xdg_surface_listener xdg_surface_listener = { xdg_surface_configure };
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) { xdg_wm_base_pong(wm_base, serial); }
static const struct xdg_wm_base_listener xdg_wm_base_listener = { xdg_wm_base_ping };
static void keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        if (key == 1 || key == 16) g_running = 0;              // ESC / Q
        else if (key == 33 || key == 87) {                     // F / F11
            if (g_fullscreen) xdg_toplevel_unset_fullscreen(g_toplevel);
            else xdg_toplevel_set_fullscreen(g_toplevel, NULL);
        }
        else if (key == 105 || key == 108)                     // Left / Down
            apply_preset(g_preset - 1);
        else if (key == 106 || key == 103)                     // Right / Up
            apply_preset(g_preset + 1);
    }
}
static void keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size) {}
static void keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {}
static void keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface) {}
static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {}
static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay) {}

static const struct wl_keyboard_listener keyboard_listener = { keyboard_keymap, keyboard_enter, keyboard_leave, keyboard_key, keyboard_modifiers, keyboard_repeat_info };
static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !g_keyboard) {
        g_keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(g_keyboard, &keyboard_listener, NULL);
    }
}
static const struct wl_seat_listener seat_listener = { seat_capabilities, nullptr };
static void registry_handler(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
    if (strcmp(interface, "wl_compositor") == 0) g_compositor = (struct wl_compositor *)wl_registry_bind(registry, id, &wl_compositor_interface, 1);
    else if (strcmp(interface, "xdg_wm_base") == 0) {
        g_wm_base = (struct xdg_wm_base *)wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(g_wm_base, &xdg_wm_base_listener, NULL);
    }
    else if (strcmp(interface, "wl_seat") == 0) {
        g_seat = (struct wl_seat *)wl_registry_bind(registry, id, &wl_seat_interface, 1);
        wl_seat_add_listener(g_seat, &seat_listener, NULL);
    }
    else if (strcmp(interface, "zxdg_decoration_manager_v1") == 0) {
        g_deco_manager = (struct zxdg_decoration_manager_v1 *)wl_registry_bind(registry, id, &zxdg_decoration_manager_v1_interface, 1);
    }
}
static const struct wl_registry_listener registry_listener = { registry_handler, nullptr };

/* -------------------------------------------------------------------------
 * Update and Render
 * ---------------------------------------------------------------------- */
static void update_physics() {
    g_cameraAngle += 0.01f * float(dRotation);
    if (g_cameraAngle >= 360.0f) g_cameraAngle -= 360.0f;
    for (int i = 0; i < dFluxes; i++) fluxes[i].update();
}

static void render_frame() {
    static rsTimer timer;
    double real_dt = timer.tick();
    g_sim_accum += real_dt;
    while (g_sim_accum >= SIM_DT) {
        update_physics();
        g_sim_accum -= SIM_DT;
    }

    glViewport(0, 0, g_win_width, g_win_height);
    float aspectRatio = float(g_win_width) / float(g_win_height);
    
    float fovY = 100.0f * DEG2RAD;
    float f = 1.0f / tanf(fovY / 2.0f);
    float zNear = 0.01f, zFar = 200.0f;
    memset(g_proj_mat, 0, sizeof(g_proj_mat));
    g_proj_mat[0] = f / aspectRatio;
    g_proj_mat[5] = f;
    g_proj_mat[10] = (zFar + zNear) / (zNear - zFar);
    g_proj_mat[11] = -1.0f;
    g_proj_mat[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (dGeometry == 0) {
        glEnable(GL_PROGRAM_POINT_SIZE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glEnable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
    } else if (dGeometry == 1) {
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
    } else {
        glBlendFunc(GL_ONE, GL_ONE);
        glEnable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
    }

    fn_UseProgram(g_prog);
    fn_UniformMatrix4fv(g_u_proj, 1, GL_FALSE, g_proj_mat);
    
    float M[16], T[16], R[16];
    mat4_translate(T, 0.0f, 0.0f, -2.5f);
    if (dGeometry == 1) {
        mat4_rotate(R, g_cameraAngle, 0, 1, 0);
        mat4_mul(M, T, R);
    } else {
        mat4_identity(M);
        mat4_mul(M, T, M);
    }
    fn_UniformMatrix4fv(g_u_mv, 1, GL_FALSE, M);
    fn_Uniform1i(g_u_geom, dGeometry);
    fn_Uniform1i(g_u_lit, dGeometry == 1 ? 1 : 0);
    
    float ldir[3] = {1.0f, 1.0f, 1.0f};
    float len = sqrtf(3.0f);
    ldir[0] /= len; ldir[1] /= len; ldir[2] /= len;
    fn_Uniform3fv(g_u_ldir, 1, ldir);

    std::vector<InstData> instances;
    instances.reserve(dFluxes * dParticles * dTrail);
    float cosA = cosf(g_cameraAngle * DEG2RAD);
    float sinA = sinf(g_cameraAngle * DEG2RAD);

    for (int i = 0; i < dFluxes; i++) {
        for (int j = 0; j < dParticles; j++) {
            particle &p = fluxes[i].particles[j];
            int p_idx = p.counter;
            int growth = 0;
            float luminosity = lumdiff;
            for (int k = 0; k < dTrail; k++) {
                p_idx++; if (p_idx >= dTrail) p_idx = 0;
                growth++;
                
                InstData inst;
                float r, g, b;
                hsl2rgb(p.vertices[p_idx][3], p.vertices[p_idx][4], luminosity, r, g, b);
                inst.col[0] = (unsigned char)(r * 255);
                inst.col[1] = (unsigned char)(g * 255);
                inst.col[2] = (unsigned char)(b * 255);
                inst.col[3] = 255;
                
                if (dGeometry == 1) {
                    inst.pos[0] = p.vertices[p_idx][0];
                    inst.pos[1] = p.vertices[p_idx][1];
                    inst.pos[2] = p.vertices[p_idx][2];
                } else {
                    float px = p.vertices[p_idx][0];
                    float pz = p.vertices[p_idx][2];
                    inst.pos[0] = cosA * px + sinA * pz;
                    inst.pos[1] = p.vertices[p_idx][1];
                    inst.pos[2] = cosA * pz - sinA * px;
                }
                
                float S = 1.0f;
                int age = dTrail - growth;
                if (dGeometry) {
                    if (age == 0) S = 0.259f;
                    else if (age == 1) S = 0.5f;
                    else if (age == 2) S = 0.707f;
                    else if (age == 3) S = 0.866f;
                    else if (age == 4) S = 0.966f;
                }
                
                if (dGeometry == 0) {
                    float depth = inst.pos[2];
                    float pt_scale = 0.004f;
                    if (age == 0) pt_scale = 0.001036f;
                    else if (age == 1) pt_scale = 0.002f;
                    else if (age == 2) pt_scale = 0.002828f;
                    else if (age == 3) pt_scale = 0.003464f;
                    else if (age == 4) pt_scale = 0.003864f;
                    inst.scale = float(dSize) * (depth + 200.0f) * pt_scale;
                } else {
                    inst.scale = float(dSize) * 0.005f * S;
                }
                instances.push_back(inst);
                luminosity += lumdiff;
            }
        }
    }
    
    if (!instances.empty()) {
        fn_BindBuffer(GL_ARRAY_BUFFER, g_inst_vbo);
        fn_BufferData(GL_ARRAY_BUFFER, instances.size() * sizeof(InstData), instances.data(), GL_DYNAMIC_DRAW);
        
        GLuint vbo, ibo;
        int indices = 0;
        if (dGeometry == 1) {
            vbo = g_vbo_sphere; ibo = g_ibo_sphere; indices = g_sphere_indices;
        } else {
            vbo = g_vbo_quad; ibo = g_ibo_quad; indices = g_quad_indices;
        }
        
        fn_BindBuffer(GL_ARRAY_BUFFER, vbo);
        fn_EnableVertexAttribArray(0); fn_EnableVertexAttribArray(1); fn_EnableVertexAttribArray(2);
        fn_VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PVertex), (void*)0);
        fn_VertexAttribPointer(1, 4, GL_SHORT, GL_TRUE, sizeof(PVertex), (void*)12);
        fn_VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(PVertex), (void*)20);
        
        fn_BindBuffer(GL_ARRAY_BUFFER, g_inst_vbo);
        fn_EnableVertexAttribArray(3); fn_EnableVertexAttribArray(4); fn_EnableVertexAttribArray(5);
        fn_VertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(InstData), (void*)0);
        fn_VertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(InstData), (void*)12);
        fn_VertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(InstData), (void*)16);
        
        fn_VertexAttribDivisor(3, 1); fn_VertexAttribDivisor(4, 1); fn_VertexAttribDivisor(5, 1);
        
        if (dGeometry == 0) {
            fn_DrawArraysInstanced(GL_POINTS, 0, 1, instances.size());
        } else {
            fn_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
            if (dGeometry == 2) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, g_tex);
                fn_Uniform1i(g_u_samp, 0);
            }
            fn_DrawElementsInstanced(GL_TRIANGLES, indices, GL_UNSIGNED_SHORT, 0, instances.size());
        }
        
        fn_VertexAttribDivisor(3, 0); fn_VertexAttribDivisor(4, 0); fn_VertexAttribDivisor(5, 0);
        
        fn_DisableVertexAttribArray(0); fn_DisableVertexAttribArray(1); fn_DisableVertexAttribArray(2);
        fn_DisableVertexAttribArray(3); fn_DisableVertexAttribArray(4); fn_DisableVertexAttribArray(5);
    }
    
    // The frame callback must be requested BEFORE the commit it belongs to.
    // eglSwapBuffers performs the wl_surface commit, so request the callback
    // first; otherwise the request is never committed and no further frames
    // are delivered (the animation freezes on frame 1).
    g_frame_callback = wl_surface_frame(g_surface);
    wl_callback_add_listener(g_frame_callback, &frame_listener, NULL);
    eglSwapBuffers(g_egl_display, g_egl_surface);
}

int main(int argc, char **argv) {
    int preset = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--preset") == 0 && i + 1 < argc) {
            int p = atoi(argv[++i]);
            if (p >= 1 && p <= 6) preset = p;
        } else if (strcmp(argv[i], "--regular") == 0) preset = 1;
        else if (strcmp(argv[i], "--hypnotic") == 0) preset = 2;
        else if (strcmp(argv[i], "--insane") == 0) preset = 3;
        else if (strcmp(argv[i], "--sparklers") == 0) preset = 4;
        else if (strcmp(argv[i], "--paradigm") == 0) preset = 5;
        else if (strcmp(argv[i], "--fusion") == 0) preset = 6; // fusion is 6, actually galactic
    }
    apply_preset(preset);

    g_display = wl_display_connect(NULL);
    struct wl_registry *registry = wl_display_get_registry(g_display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(g_display);
    wl_display_roundtrip(g_display);

    g_egl_display = eglGetDisplay((EGLNativeDisplayType)g_display);
    if (g_egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "Failed to get EGL display\n");
        return 1;
    }
    if (!eglInitialize(g_egl_display, NULL, NULL)) {
        fprintf(stderr, "Failed to initialize EGL\n");
        return 1;
    }

    g_surface = wl_compositor_create_surface(g_compositor);
    g_xdg_surface = xdg_wm_base_get_xdg_surface(g_wm_base, g_surface);
    xdg_surface_add_listener(g_xdg_surface, &xdg_surface_listener, NULL);
    g_toplevel = xdg_surface_get_toplevel(g_xdg_surface);
    xdg_toplevel_add_listener(g_toplevel, &xdg_toplevel_listener, NULL);
    xdg_toplevel_set_title(g_toplevel, "flux");
    xdg_toplevel_set_app_id(g_toplevel, "flux");

    if (g_deco_manager) {
        g_toplevel_deco = zxdg_decoration_manager_v1_get_toplevel_decoration(g_deco_manager, g_toplevel);
        zxdg_toplevel_decoration_v1_set_mode(g_toplevel_deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
    wl_surface_commit(g_surface);

    while (g_running && wl_display_dispatch(g_display) != -1) { }

    delete[] fluxes;
    if (g_egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(g_egl_display, g_egl_surface);
        eglDestroyContext(g_egl_display, g_egl_context);
        eglTerminate(g_egl_display);
    }
    if (g_egl_window) wl_egl_window_destroy(g_egl_window);
    if (g_toplevel_deco) zxdg_toplevel_decoration_v1_destroy(g_toplevel_deco);
    xdg_toplevel_destroy(g_toplevel);
    xdg_surface_destroy(g_xdg_surface);
    wl_surface_destroy(g_surface);
    if (g_keyboard) wl_keyboard_destroy(g_keyboard);
    if (g_seat) wl_seat_destroy(g_seat);
    xdg_wm_base_destroy(g_wm_base);
    wl_compositor_destroy(g_compositor);
    wl_registry_destroy(registry);
    wl_display_disconnect(g_display);
    return 0;
}
