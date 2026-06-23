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
#include <unistd.h>
#include <time.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glu.h>

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

    bool inViewVolume(float* pos, float radius) {
        if (pos[2] < -(farplane+radius)) return false;
        for (int i = 0; i < 4; i++)
            if (pos[0]*cullVec[i][0]+pos[1]*cullVec[i][1]+pos[2]*cullVec[i][2] < -radius)
                return false;
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
static unsigned int list_base;
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
 * Torus geometry
 * ---------------------------------------------------------------------- */

static void makeTorus(int smooth, int longitude, int latitude,
                      float centerradius, float thickradius)
{
    int i, j;
    float r, rr, z, zz, cosa, sina, cosn, cosnn, sinn, sinnn;
    float ncosa, nsina, u, v1, v2, ustep, vstep, temp;
    float oldcosa=0, oldsina=0, oldncosa=0, oldnsina=0;
    float oldcosn=0, oldcosnn=0, oldsinn=0, oldsinnn=0;

    glShadeModel(smooth ? GL_SMOOTH : GL_FLAT);

    vstep = 1.0f / (float)latitude;
    ustep = (float)(int)(centerradius/thickradius + 0.5f) / (float)longitude;
    v2 = 0.0f;

    for (i = 0; i < latitude; i++) {
        temp = PIx2 * (float)i / (float)latitude;
        cosn = cosf(temp); sinn = sinf(temp);
        temp = PIx2 * (float)(i+1) / (float)latitude;
        cosnn = cosf(temp); sinnn = sinf(temp);
        r  = centerradius + thickradius * cosn;
        rr = centerradius + thickradius * cosnn;
        z  = thickradius * sinn;
        zz = thickradius * sinnn;
        if (!smooth) {
            temp = PIx2 * ((float)i + 0.5f) / (float)latitude;
            cosn = cosnn = cosf(temp);
            sinn = sinnn = sinf(temp);
        }
        v1 = v2; v2 += vstep; u = 0.0f;
        glBegin(GL_TRIANGLE_STRIP);
        for (j = 0; j < longitude; j++) {
            temp = PIx2 * (float)j / (float)longitude;
            cosa = cosf(temp); sina = sinf(temp);
            if (smooth) { ncosa = cosa; nsina = sina; }
            else {
                temp = PIx2 * ((float)j - 0.5f) / (float)longitude;
                ncosa = cosf(temp); nsina = sinf(temp);
            }
            if (j == 0) {
                oldcosa=cosa; oldsina=sina; oldncosa=ncosa; oldnsina=nsina;
                oldcosn=cosn; oldcosnn=cosnn; oldsinn=sinn; oldsinnn=sinnn;
            }
            glNormal3f(cosnn*ncosa, cosnn*nsina, sinnn);
            glTexCoord2f(u, v2);
            glVertex3f(cosa*rr, sina*rr, zz);
            glNormal3f(cosn*ncosa, cosn*nsina, sinn);
            glTexCoord2f(u, v1);
            glVertex3f(cosa*r, sina*r, z);
            u += ustep;
        }
        glNormal3f(oldcosnn*oldncosa, oldcosnn*oldnsina, oldsinnn);
        glTexCoord2f(u, v2);
        glVertex3f(oldcosa*rr, oldsina*rr, zz);
        glNormal3f(oldcosn*oldncosa, oldcosn*oldnsina, oldsinn);
        glTexCoord2f(u, v1);
        glVertex3f(oldcosa*r, oldsina*r, z);
        glEnd();
    }
}

static void setMaterialAttribs() {
    /* Original logic: random colour for no-texture and shiny/ghostly/circuit/doughnut;
       industrial picks one of the two textures randomly; others do nothing
       (sphere-mapped textures use glColor white set in draw()). */
    if (dTexture == 0 || dTexture >= 5)
        glColor3f(rsRandf(1.0f), rsRandf(1.0f), rsRandf(1.0f));
    if (dTexture == 1)
        glBindTexture(GL_TEXTURE_2D, texture_id[rsRandi(2)]);
}

static void makeLatticeObjects() {
    int i, d = 0;
    float thick = (float)dThick * 0.001f;

    list_base = glGenLists(NUMOBJECTS);

    for (i = 0; i < NUMOBJECTS; i++) {
        glNewList(list_base + i, GL_COMPILE);

        /* Industrial uses 2 textures; others all use texture_id[0] */
        if (dTexture >= 2)
            glBindTexture(GL_TEXTURE_2D, texture_id[0]);

        if (d < dDensity) {
            glPushMatrix();
            setMaterialAttribs();
            glTranslatef(-0.25f, -0.25f, -0.25f);
            if (rsRandi(2)) glRotatef(180.0f, 1,0,0);
            makeTorus(dSmooth, dLongitude, dLatitude, 0.36f-thick, thick);
            glPopMatrix();
        }
        d = (d+37) % 100;

        if (d < dDensity) {
            glPushMatrix();
            setMaterialAttribs();
            glTranslatef(0.25f, -0.25f, -0.25f);
            if (rsRandi(2)) glRotatef(90.0f, 1,0,0);
            else            glRotatef(-90.0f, 1,0,0);
            makeTorus(dSmooth, dLongitude, dLatitude, 0.36f-thick, thick);
            glPopMatrix();
        }
        d = (d+37) % 100;

        if (d < dDensity) {
            glPushMatrix();
            setMaterialAttribs();
            glTranslatef(0.25f, -0.25f, 0.25f);
            if (rsRandi(2)) glRotatef(90.0f, 0,1,0);
            else            glRotatef(-90.0f, 0,1,0);
            makeTorus(dSmooth, dLongitude, dLatitude, 0.36f-thick, thick);
            glPopMatrix();
        }
        d = (d+37) % 100;

        if (d < dDensity) {
            glPushMatrix();
            setMaterialAttribs();
            glTranslatef(0.25f, 0.25f, 0.25f);
            if (rsRandi(2)) glRotatef(180.0f, 1,0,0);
            makeTorus(dSmooth, dLongitude, dLatitude, 0.36f-thick, thick);
            glPopMatrix();
        }
        d = (d+37) % 100;

        if (d < dDensity) {
            glPushMatrix();
            setMaterialAttribs();
            glTranslatef(-0.25f, 0.25f, 0.25f);
            if (rsRandi(2)) glRotatef(90.0f, 1,0,0);
            else            glRotatef(-90.0f, 1,0,0);
            makeTorus(dSmooth, dLongitude, dLatitude, 0.36f-thick, thick);
            glPopMatrix();
        }
        d = (d+37) % 100;

        if (d < dDensity) {
            glPushMatrix();
            setMaterialAttribs();
            glTranslatef(-0.25f, 0.25f, -0.25f);
            if (rsRandi(2)) glRotatef(90.0f, 0,1,0);
            else            glRotatef(-90.0f, 0,1,0);
            makeTorus(dSmooth, dLongitude, dLatitude, 0.36f-thick, thick);
            glPopMatrix();
        }

        glEndList();
        d = (d+37) % 100;
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
 * Scene render  (uses whatever state update_physics last wrote)
 * ---------------------------------------------------------------------- */

static void render_scene() {
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(g_rotMat);
    glTranslatef(-g_xyz[0], -g_xyz[1], -g_xyz[2]);

    glColor3f(1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (dTexture == 2 || dTexture == 3 || dTexture == 4 ||
        dTexture == 5 || dTexture == 6) {
        glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
        glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
        glEnable(GL_TEXTURE_GEN_S);
        glEnable(GL_TEXTURE_GEN_T);
    }

    int i, j, k;
    for (i = globalxyz[0]-g_drawDepth; i <= globalxyz[0]+g_drawDepth; i++) {
        for (j = globalxyz[1]-g_drawDepth; j <= globalxyz[1]+g_drawDepth; j++) {
            for (k = globalxyz[2]-g_drawDepth; k <= globalxyz[2]+g_drawDepth; k++) {
                rsVec tv;
                tv[0] = (float)i - g_xyz[0];
                tv[1] = (float)j - g_xyz[1];
                tv[2] = (float)k - g_xyz[2];
                float tpos[3];
                tpos[0] = tv[0]*g_rotMat[0]+tv[1]*g_rotMat[4]+tv[2]*g_rotMat[8];
                tpos[1] = tv[0]*g_rotMat[1]+tv[1]*g_rotMat[5]+tv[2]*g_rotMat[9];
                tpos[2] = tv[0]*g_rotMat[2]+tv[1]*g_rotMat[6]+tv[2]*g_rotMat[10];
                if (theCamera->inViewVolume(tpos, 0.9f)) {
                    glPushMatrix();
                    glTranslatef((float)i, (float)j, (float)k);
                    glCallList(latticeGrid[myMod(i)][myMod(j)][myMod(k)]);
                    glPopMatrix();
                }
            }
        }
    }

    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
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

    makeLatticeObjects();
    for (i = 0; i < LATSIZE; i++)
        for (j = 0; j < LATSIZE; j++)
            for (k = 0; k < LATSIZE; k++)
                latticeGrid[i][j][k] = list_base + rsRandi(NUMOBJECTS);

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
    glDeleteLists(list_base, NUMOBJECTS);

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
    makeLatticeObjects();
    for (int i = 0; i < LATSIZE; i++)
        for (int j = 0; j < LATSIZE; j++)
            for (int k = 0; k < LATSIZE; k++)
                latticeGrid[i][j][k] = list_base + rsRandi(NUMOBJECTS);

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

static void render_frame() {
    /* Accumulate real elapsed time, then step physics at a fixed 60 fps rate.
     * On a 120 Hz display we render twice as often but physics still advance
     * at the same pace as the original screensaver on a 60 Hz machine. */
    float real_dt = (float)g_wall_timer.tick();
    g_sim_accum += real_dt;
    if (g_sim_accum > 4.0f * SIM_DT)   /* cap: recover from pauses without lurching */
        g_sim_accum = 4.0f * SIM_DT;
    while (g_sim_accum >= SIM_DT) {
        frameTime = SIM_DT;
        update_physics();
        g_sim_accum -= SIM_DT;
    }

    render_scene();

    /* Attach next frame callback before swap so it is included in the commit */
    struct wl_callback *next = wl_surface_frame(g_surface);
    wl_callback_add_listener(next, &frame_listener, NULL);
    eglSwapBuffers(g_egl_display, g_egl_surface);
}

static void frame_done(void *, struct wl_callback *cb, uint32_t) {
    wl_callback_destroy(cb);
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
        else {
            fprintf(stderr,
                "Usage: %s [--preset NAME] [--speed N] [--depth N] [--density N]\n"
                "          [--thick N] [--fov N] [--longitude N] [--latitude N]\n"
                "          [--pathrand N] [--smooth|--no-smooth] [--fog|--no-fog]\n"
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

    wl_surface_commit(g_surface);

    /* --- EGL context (before configure so we can create the surface after) --- */
    if (!init_egl()) return 1;

    /* --- Wait for initial configure --- */
    while (!g_configured)
        wl_display_dispatch(g_display);

    if (g_needs_resize) {
        g_win_width  = g_new_width;
        g_win_height = g_new_height;
        g_needs_resize = 0;
    }

    /* --- Create EGL window surface now that we know the size --- */
    if (!create_egl_surface()) return 1;

    /* --- Initialize OpenGL state + screensaver --- */
    initSaver();

    /* --- Frame-callback render loop --- */
    g_wall_timer.tick();   /* prime: discard time elapsed during startup */
    render_frame();        /* first frame; attaches callback chain to compositor */

    while (g_running) {
        if (wl_display_dispatch(g_display) < 0) break;
    }

    /* --- Cleanup --- */
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
