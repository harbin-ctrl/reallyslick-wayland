/*
 * Colorfire screensaver - Wayland/EGL port
 * Original Copyright (C) 1999 Andreas Gustafsson (GPL-2.0-or-later)
 * Ported to Linux by Tugrul Galatali <tugrul@galatali.com>
 * Wayland port: native Wayland client with EGL/OpenGL, no X11
 *
 * Build:  make
 * Run:    ./colorfire [options]
 * Quit:   Escape or close button
 * Keys:   F / F11 = toggle fullscreen, Q / Esc = quit, Left/Right = cycle textures
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
#include "colorfire_textures.h"

/* -------------------------------------------------------------------------
 * Math library & utility
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

/* Column-major 4×4 matrix helpers */
static void mat4_identity(float M[16]) {
    memset(M, 0, 64); M[0]=M[5]=M[10]=M[15]=1.0f;
}
static void mat4_translate(float M[16], float tx, float ty, float tz) {
    mat4_identity(M); M[12]=tx; M[13]=ty; M[14]=tz;
}
static void mat4_scale(float M[16], float sx, float sy, float sz) {
    mat4_identity(M); M[0]=sx; M[5]=sy; M[10]=sz;
}
static void mat4_rotate(float M[16], float deg, float ax, float ay, float az) {
    float a=deg*0.0174532925f, c=cosf(a), s=sinf(a), ic=1.0f-c;
    float len=sqrtf(ax*ax+ay*ay+az*az); 
    if (len > 1e-7f) { ax/=len; ay/=len; az/=len; }
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
 * OpenGL VBO infrastructure (GL 1.5)
 * ---------------------------------------------------------------------- */

struct GVertex {
    float pos[3];
    float tc[2];
};

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER  0x8892
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW   0x88B4
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

/* Shader function pointers */
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
static PFNGLUNIFORM4FVPROC                fn_Uniform4fv            = nullptr;
static PFNGLUNIFORMMATRIX4FVPROC          fn_UniformMatrix4fv      = nullptr;
static PFNGLENABLEVERTEXATTRIBARRAYPROC   fn_EnableVertexAttribArray  = nullptr;
static PFNGLDISABLEVERTEXATTRIBARRAYPROC  fn_DisableVertexAttribArray = nullptr;
static PFNGLVERTEXATTRIBPOINTERPROC       fn_VertexAttribPointer   = nullptr;
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
    fn_Uniform4fv           = (PFNGLUNIFORM4FVPROC)              eglGetProcAddress("glUniform4fv");
    fn_UniformMatrix4fv     = (PFNGLUNIFORMMATRIX4FVPROC)        eglGetProcAddress("glUniformMatrix4fv");
    fn_EnableVertexAttribArray  = (PFNGLENABLEVERTEXATTRIBARRAYPROC) eglGetProcAddress("glEnableVertexAttribArray");
    fn_DisableVertexAttribArray = (PFNGLDISABLEVERTEXATTRIBARRAYPROC)eglGetProcAddress("glDisableVertexAttribArray");
    fn_VertexAttribPointer  = (PFNGLVERTEXATTRIBPOINTERPROC)     eglGetProcAddress("glVertexAttribPointer");
    fn_DeleteShader         = (PFNGLDELETESHADERPROC)            eglGetProcAddress("glDeleteShader");
    fn_DeleteProgram        = (PFNGLDELETEPROGRAMPROC)           eglGetProcAddress("glDeleteProgram");
}

/* -------------------------------------------------------------------------
 * Colorfire State & Constants
 * ---------------------------------------------------------------------- */

#define NR_WAVES 24
#define TEXSIZE 256

static int dTexture = 1; /* Default to smoke texture */

static float wrot[NR_WAVES], wtime[NR_WAVES], wr[NR_WAVES], wg[NR_WAVES], wb[NR_WAVES], wspd[NR_WAVES], wmax[NR_WAVES];
static float v1 = 0, v2 = 0, v3 = 0;

static GLuint g_quad_vbo = 0;
static GLuint g_quad_ibo = 0;
static GLuint g_prog     = 0;
static GLuint g_tex_id   = 0;

static float g_proj_mat[16];
static GLint g_u_proj, g_u_mv, g_u_color, g_u_tex_mode;

static void initWave (int nr) {
    wtime[nr] = 0;
    wrot[nr] = rsRandf (360.0f);
    wr[nr] = rsRandf (1.0f);
    wg[nr] = rsRandf (1.0f);
    wb[nr] = rsRandf (1.0f);
    wspd[nr] = 0.5f + rsRandf (0.3f);
    wmax[nr] = 1.0f + rsRandf (1.0f);
}

/* -------------------------------------------------------------------------
 * Projection Setup
 * ---------------------------------------------------------------------- */

static void setupProjection(int w, int h) {
    float aspect = (float)w / (float)h;
    float fov = 55.0f;
    float near = 1.0f;
    float far = 20.0f;
    float f = cosf(fov * 0.5f * 0.0174532925f) / sinf(fov * 0.5f * 0.0174532925f);

    memset(g_proj_mat, 0, sizeof(g_proj_mat));
    g_proj_mat[0]  = f / aspect;
    g_proj_mat[5]  = f;
    g_proj_mat[10] = (far + near) / (near - far);
    g_proj_mat[11] = -1.0f;
    g_proj_mat[14] = (2.0f * far * near) / (near - far);

    glViewport(0, 0, w, h);
}

/* -------------------------------------------------------------------------
 * Texture Setup
 * ---------------------------------------------------------------------- */

static void initTextures() {
    if (g_tex_id) glDeleteTextures(1, &g_tex_id);
    glGenTextures(1, &g_tex_id);
    glBindTexture(GL_TEXTURE_2D, g_tex_id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    if (dTexture == 1) {
        gluBuild2DMipmaps(GL_TEXTURE_2D, 1, TEXSIZE, TEXSIZE, GL_RGB, GL_UNSIGNED_BYTE, (const unsigned char *)smokemap);
    } else if (dTexture == 2) {
        gluBuild2DMipmaps(GL_TEXTURE_2D, 1, TEXSIZE, TEXSIZE, GL_RGB, GL_UNSIGNED_BYTE, (const unsigned char *)ripplemap);
    } else {
        unsigned char img[] = { 255 };
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 1, 1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, img);
    }
}

/* -------------------------------------------------------------------------
 * Shader setup
 * ---------------------------------------------------------------------- */

static void build_shader_program() {
    static const char VERT_SRC[] =
        "#version 140\n"
        "in vec3 a_pos;\n"
        "in vec2 a_tc;\n"
        "uniform mat4 u_proj;\n"
        "uniform mat4 u_mv;\n"
        "out vec2 v_tc;\n"
        "void main() {\n"
        "    gl_Position = u_proj * u_mv * vec4(a_pos, 1.0);\n"
        "    v_tc = a_tc;\n"
        "}\n";

    static const char FRAG_SRC[] =
        "#version 140\n"
        "in vec2 v_tc;\n"
        "uniform vec4 u_color;\n"
        "uniform sampler2D u_samp;\n"
        "uniform int u_tex_mode;\n" /* 0=None, 1=Smoke, 2=Ripples, 3=Smooth */
        "out vec4 frag;\n"
        
        "vec4 textureSmooth(sampler2D sampler, vec2 uv) {\n"
        "    vec2 texSize = vec2(textureSize(sampler, 0));\n"
        "    vec2 uv_scaled = uv * texSize;\n"
        "    vec2 i = floor(uv_scaled - 0.5);\n"
        "    vec2 f = fract(uv_scaled - 0.5);\n"
        "    vec2 w = f * f * (3.0 - 2.0 * f);\n"
        "    vec2 uv_smooth = (i + 0.5 + w) / texSize;\n"
        "    return texture(sampler, uv_smooth);\n"
        "}\n"
        
        "void main() {\n"
        "    if (u_tex_mode == 3) {\n"
        "        float dx = v_tc.x - 0.5;\n"
        "        float dy = v_tc.y - 0.5;\n"
        "        float dist = sqrt(dx * dx + dy * dy);\n"
        "        float intensity = 0.0;\n"
        "        if (dist < 0.5) {\n"
        "            float d = 1.0 - (dist / 0.5);\n"
        "            intensity = d * d;\n"
        "        }\n"
        "        frag = vec4(u_color.rgb * intensity, 1.0);\n"
        "    }\n"
        "    else if (u_tex_mode == 0) {\n"
        "        float intensity = mix(v_tc.x, 1.0 - v_tc.x, v_tc.y);\n"
        "        frag = vec4(u_color.rgb * intensity, 1.0);\n"
        "    }\n"
        "    else {\n"
        "        vec4 tex = textureSmooth(u_samp, v_tc);\n"
        "        frag = vec4(u_color.rgb * tex.rgb, 1.0);\n"
        "    }\n"
        "}\n";

    auto compile = [](GLenum type, const char *src) -> GLuint {
        GLuint s = fn_CreateShader(type);
        fn_ShaderSource(s, 1, &src, NULL);
        fn_CompileShader(s);
        GLint ok; fn_GetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512]; fn_GetShaderInfoLog(s, 512, NULL, log);
            fprintf(stderr, "colorfire shader: %s\n", log);
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
        char log[512]; fn_GetProgramInfoLog(g_prog, 512, NULL, log);
        fprintf(stderr, "colorfire link: %s\n", log);
    }
    fn_DeleteShader(vs);
    fn_DeleteShader(fs);

    fn_UseProgram(g_prog);
    g_u_proj     = fn_GetUniformLocation(g_prog, "u_proj");
    g_u_mv       = fn_GetUniformLocation(g_prog, "u_mv");
    g_u_color    = fn_GetUniformLocation(g_prog, "u_color");
    g_u_tex_mode = fn_GetUniformLocation(g_prog, "u_tex_mode");

    fn_Uniform1i(fn_GetUniformLocation(g_prog, "u_samp"), 0);
}

/* -------------------------------------------------------------------------
 * OpenGL State Init
 * ---------------------------------------------------------------------- */

static void initSaver() {
    srand((unsigned)time(NULL));

    /* VBO Generation for Quad */
    static const GVertex QUAD_VERTS[4] = {
        {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 1.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 1.0f,  1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-1.0f,  1.0f, 0.0f}, {0.0f, 1.0f}}
    };
    static const unsigned int QUAD_INDICES[6] = {
        0, 1, 2,
        2, 3, 0
    };

    fn_GenBuffers(1, &g_quad_vbo);
    fn_BindBuffer(GL_ARRAY_BUFFER, g_quad_vbo);
    fn_BufferData(GL_ARRAY_BUFFER, sizeof(QUAD_VERTS), QUAD_VERTS, GL_STATIC_DRAW);
    fn_BindBuffer(GL_ARRAY_BUFFER, 0);

    fn_GenBuffers(1, &g_quad_ibo);
    fn_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_quad_ibo);
    fn_BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(QUAD_INDICES), QUAD_INDICES, GL_STATIC_DRAW);
    fn_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    glClearColor (0.0, 0.0, 0.0, 0.0);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE); /* Additive blending */

    for (int i = 0; i < NR_WAVES; i++) {
        initWave(i);
        wtime[i] = 3.0f + rsRandf(1.0f);
    }

    initTextures();
    build_shader_program();
}

/* -------------------------------------------------------------------------
 * Physics Simulation (Fixed step 60fps)
 * ---------------------------------------------------------------------- */

static void update_physics(float dt) {
    v1 += dt * 5.0f;
    if (v1 > 360.0f) v1 -= 360.0f;
    v2 += dt * 10.0f;
    if (v2 > 360.0f) v2 -= 360.0f;
    v3 += dt * 7.0f;
    if (v3 > 360.0f) v3 -= 360.0f;
}

/* -------------------------------------------------------------------------
 * Scene Render
 * ---------------------------------------------------------------------- */

static void drawWave (int nr, float fDeltaTime, const float MV_global[16]) {
    float colMod = 1.0f;
    if (wtime[nr] > (wmax[nr] - 1.0f))
        colMod = wmax[nr] - wtime[nr];

    /* Calculate modelview for this wave quad */
    float S[16], RX[16], RY[16], RZ[16], T1[16], T2[16], T3[16], MV_wave[16];
    
    mat4_scale(S, wtime[nr], wtime[nr], wtime[nr]);
    mat4_rotate(RX, wrot[nr], 1.0f, 0.0f, 0.0f);
    mat4_rotate(RY, wrot[nr], 0.0f, 1.0f, 0.0f);
    mat4_rotate(RZ, wrot[nr], 0.0f, 0.0f, 1.0f);
    
    mat4_mul(T1, RX, RY);
    mat4_mul(T2, T1, RZ);
    mat4_mul(T3, S, T2);
    mat4_mul(MV_wave, MV_global, T3);

    /* Set uniforms */
    fn_UniformMatrix4fv(g_u_mv, 1, GL_FALSE, MV_wave);
    
    float color[4] = {colMod * wr[nr], colMod * wg[nr], colMod * wb[nr], 1.0f};
    fn_Uniform4fv(g_u_color, 1, color);

    /* Draw static VBO quad */
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    /* Update time */
    wtime[nr] += fDeltaTime * wspd[nr];
    if (wtime[nr] > wmax[nr])
        initWave (nr);
}

static void render_scene(float frameTime) {
    glClear(GL_COLOR_BUFFER_BIT);

    fn_UseProgram(g_prog);
    fn_UniformMatrix4fv(g_u_proj, 1, GL_FALSE, g_proj_mat);

    /* Texture settings */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_tex_id);
    
    fn_Uniform1i(g_u_tex_mode, dTexture);

    /* Global rotation matrices */
    float T[16], RX[16], RY[16], RZ[16], T1[16], T2[16], MV_global[16];
    mat4_translate(T, 0.0f, 0.0f, -3.0f);
    mat4_rotate(RX, v1, 1.0f, 0.0f, 0.0f);
    mat4_rotate(RY, v2, 0.0f, 1.0f, 0.0f);
    mat4_rotate(RZ, v3, 0.0f, 0.0f, 1.0f);
    
    mat4_mul(T1, T, RX);
    mat4_mul(T2, T1, RY);
    mat4_mul(MV_global, T2, RZ);

    /* Draw static VBO attributes setup */
    fn_BindBuffer(GL_ARRAY_BUFFER, g_quad_vbo);
    fn_EnableVertexAttribArray(0);
    fn_EnableVertexAttribArray(1);
    fn_VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GVertex), (const void*)0);
    fn_VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GVertex), (const void*)12);

    fn_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_quad_ibo);

    for (int i = 0; i < NR_WAVES; i++) {
        drawWave(i, frameTime, MV_global);
    }

    fn_DisableVertexAttribArray(0);
    fn_DisableVertexAttribArray(1);
    fn_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    fn_BindBuffer(GL_ARRAY_BUFFER, 0);
}

/* -------------------------------------------------------------------------
 * Wayland + EGL Scaffolding
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

static void applyTextureMode(int mode) {
    dTexture = mode;
    initTextures();
    
    char title[128];
    const char *texNames[] = {"None", "Smoke", "Ripples", "Smooth"};
    snprintf(title, sizeof(title), "Colorfire — Texture: %s (<- -> cycle, F fullscreen)", texNames[dTexture]);
    xdg_toplevel_set_title(g_toplevel, title);
}

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
    } else if (key == 105 || key == 106) {  /* KEY_LEFT / KEY_RIGHT */
        int mode = (dTexture + (key == 106 ? 1 : 3)) % 4;
        applyTextureMode(mode);
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

/* -------------------------------------------------------------------------
 * Frame-callback paced render loop
 * ---------------------------------------------------------------------- */

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

static const float SIM_DT  = 1.0f / 60.0f;
static float  g_sim_accum  = 0.0f;
static rsTimer g_wall_timer;

static void render_frame() {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    float real_dt = (float)g_wall_timer.tick();
    g_sim_accum += real_dt;
    if (g_sim_accum > 4.0f * SIM_DT) g_sim_accum = 4.0f * SIM_DT;
    while (g_sim_accum >= SIM_DT) {
        update_physics(SIM_DT);
        g_sim_accum -= SIM_DT;
    }

    render_scene(real_dt);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double frame_ms = ((t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9) * 1000.0;

    g_perf.frames++;
    g_perf.frame_ms_sum += frame_ms;

    double elapsed = (t1.tv_sec - g_perf.window_start.tv_sec) + (t1.tv_nsec - g_perf.window_start.tv_nsec) * 1e-9;
    if (elapsed >= 1.0) {
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
        setupProjection(g_win_width, g_win_height);
    }

    render_frame();
}

int main(int argc, char *argv[]) {
    dTexture = rsRandi(3) + 1; /* Random start texture */

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--texture") || !strcmp(argv[i], "-t")) {
            if (i + 1 < argc) dTexture = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--smoke")) {
            dTexture = 1;
        }
        else if (!strcmp(argv[i], "--ripples")) {
            dTexture = 2;
        }
        else if (!strcmp(argv[i], "--smooth")) {
            dTexture = 3;
        }
        else if (!strcmp(argv[i], "--fullscreen") || !strcmp(argv[i], "-fullscreen")) {
            g_start_fullscreen = 1;
        }
        else if (!strcmp(argv[i], "--benchmark") || !strcmp(argv[i], "-benchmark")) {
            g_benchmark_mode = true;
        }
        else {
            fprintf(stderr, "Usage: %s [options]\n"
                            "Options:\n"
                            "  --texture, -t <0-3>  Texture type (0=None, 1=Smoke, 2=Ripples, 3=Smooth)\n"
                            "  --smoke              Short for --texture 1\n"
                            "  --ripples            Short for --texture 2\n"
                            "  --smooth             Short for --texture 3\n"
                            "  --fullscreen, -fullscreen Start in fullscreen\n"
                            "  --benchmark, -benchmark   Non-blocking benchmark mode\n", argv[0]);
            return 1;
        }
    }

    g_display = wl_display_connect(NULL);
    if (!g_display) {
        fprintf(stderr, "colorfire: cannot connect to Wayland display\n");
        return 1;
    }

    struct wl_registry *registry = wl_display_get_registry(g_display);
    wl_registry_add_listener(registry, &registry_lst, NULL);
    wl_display_roundtrip(g_display);

    if (!g_compositor || !g_wm_base) {
        fprintf(stderr, "colorfire: missing required Wayland globals\n");
        return 1;
    }

    g_surface = wl_compositor_create_surface(g_compositor);
    g_xdg_surface = xdg_wm_base_get_xdg_surface(g_wm_base, g_surface);
    xdg_surface_add_listener(g_xdg_surface, &xdg_surface_lst, NULL);
    g_toplevel = xdg_surface_get_toplevel(g_xdg_surface);
    xdg_toplevel_add_listener(g_toplevel, &toplevel_lst, NULL);

    char title[128];
    const char *texNames[] = {"None", "Smoke", "Ripples", "Smooth"};
    snprintf(title, sizeof(title), "Colorfire — Texture: %s (<- -> cycle, F fullscreen)", texNames[dTexture]);
    xdg_toplevel_set_title(g_toplevel, title);
    xdg_toplevel_set_app_id(g_toplevel, "colorfire");

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

    load_vbo_fns();
    load_shader_fns();
    initSaver();
    setupProjection(g_win_width, g_win_height);

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

    /* Cleanup */
    if (g_frame_callback) {
        wl_callback_destroy(g_frame_callback);
        g_frame_callback = NULL;
    }
    if (g_prog)     fn_DeleteProgram(g_prog);
    if (g_quad_vbo) fn_DeleteBuffers(1, &g_quad_vbo);
    if (g_quad_ibo) fn_DeleteBuffers(1, &g_quad_ibo);
    glDeleteTextures(1, &g_tex_id);

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
