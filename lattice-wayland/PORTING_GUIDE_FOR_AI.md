# Porting Really Slick Screensavers to Wayland/Modern OpenGL
## A technical briefing for AI assistants

---

## What this is

Really Slick Screensavers (RSS) by Terence Welsh are OpenGL screensavers from
~1999–2010. Source: https://www.reallyslick.com/  GPL-2.0+.
They use OpenGL 1.x / fixed-function pipeline + xscreensaver.

**Goal:** port each one to a standalone native Wayland client with modern GL.

**Reference implementation:** `/home/erik/lattice-wayland/lattice.cpp`
This is a complete, working, optimized port of the Lattice screensaver.
Read it before writing anything. It is the template.

**Hardware target:** Orange Pi 800, RK3399 SoC, Mali-T860MP4, panfrost driver,
Mesa 25.2.8, OpenGL 3.1 / GLSL 1.40, Armbian, aarch64.
Also expected to run on Raspberry Pi 400 (VideoCore VI, v3d driver, same GL version).

---

## Original RSS source structure

Every RSS screensaver shares these patterns:

```cpp
// Math: rsVec (3-float vector), rsQuat (quaternion), rsMath.h
rsVec v(x, y, z);  v.normalize();  v.cross(other);  v.dot(other);

// Random: uses rand() seeded from time(NULL)
rsRandf(max)   // returns float in [0, max)
rsRandi(max)   // returns int in [0, max)

// Timer: rsTimer using gettimeofday or QueryPerformanceCounter
float dt = timer.tick();  // seconds since last call

// Geometry: OpenGL display lists
glNewList(n, GL_COMPILE);
  // geometry here
glEndList();
glCallList(n);

// Textures: raw arrays in texture.h (TEXSIZE×TEXSIZE, various formats)

// Entry point: init(), draw(), done()  — called by xscreensaver wrapper
```

**Do NOT use:** rsVec, rsQuat, rsMath, rsTimer, xscreensaver wrapper.
Port the math classes inline (they're small). See lattice.cpp lines 64–175
for the complete port of rsVec, rsQuat, rsTimer.

---

## Wayland boilerplate — DO NOT REWRITE

The Wayland+EGL+frame-callback scaffolding is identical for every port.
Copy it verbatim from `lattice.cpp`. Key pieces:

### Globals needed
```cpp
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
static EGLDisplay  g_egl_display;
static EGLContext  g_egl_context;
static EGLSurface  g_egl_surface;
static EGLConfig   g_egl_config;
static int g_win_width=800, g_win_height=600;
static int g_configured=0, g_running=1, g_needs_resize=0;
```

### Frame callback render loop (CRITICAL — do not use sleep/busy-poll)
```
wl_surface_frame → frame_done callback → render_frame() → eglSwapBuffers
                                                         → register next frame callback
```
This is the correct Wayland-native pacing. See lattice.cpp `frame_done()` and
`render_frame()`. Copy exactly. The physics accumulator pattern:
```cpp
static const float SIM_DT = 1.0f / 60.0f;
static float g_sim_accum = 0.0f;
// in render_frame():
g_sim_accum += real_dt;
while (g_sim_accum >= SIM_DT) { update_physics(); g_sim_accum -= SIM_DT; }
render_scene();
```
This separates physics rate (60 Hz equivalent) from display refresh rate.

### EGL context creation
```cpp
EGLint ctx_attribs[] = { EGL_NONE };  // no version request = take what driver gives
g_egl_context = eglCreateContext(g_egl_display, g_egl_config, EGL_NO_CONTEXT, ctx_attribs);
eglSwapInterval(g_egl_display, 0);  // frame callbacks own timing; never block in EGL
```

### Build system
Copy the Makefile from lattice-wayland. It handles:
- xdg-shell protocol stubs (wayland-scanner)
- xdg-decoration protocol stubs
- pkg-config for wayland-client, wayland-egl, egl, gl
- Links: -lGLU -lm

---

## OpenGL modernization — the required sequence

**Do all four steps.** Skipping any leaves performance on the table.

### Step 1: Replace display lists with VBOs

**Why it matters on panfrost:** panfrost emulates display lists in software
(Mesa's CPU-side fixed-function compatibility layer). Each `glCallList` replay
processes GL bytecode through a software translator. With 1000+ calls/frame,
this alone causes 5–10× slowdown vs. direct GPU commands.

**Pattern:**
```cpp
// GVertex struct (48 bytes, matches pointer offsets below)
struct GVertex {
    float pos[3];     // offset  0
    float normal[3];  // offset 12
    float tc[2];      // offset 24
    float color[4];   // offset 32
};

// Build geometry into std::vector<GVertex> verts + std::vector<unsigned int> indices
// Bake transforms into vertex positions/normals at build time (no per-frame matrix math)
// Upload once:
glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferData(GL_ARRAY_BUFFER, verts.size()*48, verts.data(), GL_STATIC_DRAW);
glGenBuffers(1, &ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size()*4, idx.data(), GL_STATIC_DRAW);
```

Load VBO functions via eglGetProcAddress (panfrost needs this even for GL 1.5
functions). See lattice.cpp `load_vbo_fns()`.

**IBO (index buffer): always use it for meshes with shared vertices.**
For smooth-shaded tori (lon×lat grid): 6× fewer vertex shader invocations via
Post-Transform Cache. For flat-shaded: 1.5× fewer. Always worth it.

### Step 2: Replace fixed-function pipeline with GLSL shaders

**Why:** `glEnable(GL_LIGHTING)`, `glFogf()`, `GL_TEXTURE_GEN_*` are
fixed-function and slow/broken on panfrost. The vertex shader below replicates
all of them.

**Load shader functions via eglGetProcAddress.** See lattice.cpp
`load_shader_fns()` for the complete list of PFNGL* pointers needed.

#### Vertex shader template (GLSL 1.40, works on GL 3.1)
```glsl
#version 140
in vec3 a_pos;
in vec3 a_nrm;
in vec2 a_tc;
in vec4 a_col;
in vec3 a_inst;       /* per-instance world translation (divisor=1); omit if no instancing */

uniform mat4  u_proj;
uniform mat4  u_mv;
uniform int   u_lit;  /* 1=Blinn-Phong, 0=unlit */
uniform float u_shin;
uniform vec3  u_ldir; /* light direction in eye space, normalised */
uniform int   u_smap; /* 1=sphere-map texcoords */
uniform int   u_fog;
uniform float u_fog0, u_fog1;

out vec4  v_col;
out vec2  v_tc;
out float v_fog;

void main() {
    vec4 eye = u_mv * vec4(a_pos + a_inst, 1.0);
    gl_Position = u_proj * eye;
    vec3 n = normalize(mat3(u_mv) * a_nrm);
    if (u_lit == 1) {
        float d = max(dot(n, u_ldir), 0.0);
        vec3  h = normalize(u_ldir + normalize(-eye.xyz));
        float s = pow(max(dot(n, h), 0.0), max(u_shin, 1.0));
        v_col = vec4(a_col.rgb * (d + 0.2) + vec3(s), a_col.a);
    } else {
        v_col = a_col;
    }
    if (u_smap == 1) {
        vec3  r = reflect(normalize(eye.xyz), n);
        float m = 2.0 * sqrt(max(r.x*r.x + r.y*r.y + (r.z+1.0)*(r.z+1.0), 0.0001));
        v_tc = vec2(r.x/m + 0.5, r.y/m + 0.5);
    } else {
        v_tc = a_tc;
    }
    float dep = abs(eye.z);
    v_fog = (u_fog == 1) ? clamp((u_fog1-dep)/(u_fog1-u_fog0), 0.0, 1.0) : 1.0;
}
```

#### Fragment shader template
```glsl
#version 140
in vec4  v_col;
in vec2  v_tc;
in float v_fog;
uniform int       u_tex;   /* 0=none 1=modulate 2=decal(RGBA) 3=alpha-only */
uniform sampler2D u_samp;
out vec4 frag;
void main() {
    vec4 c = v_col;
    if      (u_tex == 1) c *= texture(u_samp, v_tc);
    else if (u_tex == 2) { vec4 t=texture(u_samp,v_tc); c=vec4(mix(c.rgb,t.rgb,t.a),c.a); }
    else if (u_tex == 3) c.a *= texture(u_samp, v_tc).a;  /* GL_ALPHA textures */
    frag = vec4(c.rgb * v_fog, c.a);
}
```

**Attribute locations** (bind before link):
- 0 = a_pos, 1 = a_nrm, 2 = a_tc, 3 = a_col, 4 = a_inst

**Key uniforms to set per preset/init:**
- `u_lit`: 0 for chrome/brass/ghostly modes, 1 otherwise
- `u_shin`: 50 most modes, 10 for crystal, 1 for rough/industrial
- `u_smap`: 1 for crystal/chrome/brass/shiny/ghostly textures
- `u_tex`: 1=modulate(RGB tex), 2=decal(RGBA), 3=alpha-only(GL_ALPHA tex)
- `u_fog`, `u_fog0`, `u_fog1`: from dFog, dDepth settings
- `u_ldir`: compute each frame → `(rotMat_upper3x3 * light_world_dir) / length`

**Projection matrix:** build manually (same formula as gluPerspective, column-major).
Store in `g_proj_mat[16]`, upload each frame via `glUniformMatrix4fv`.

**MV matrix per frame:**
```cpp
float T[16], MV[16];
mat4_translate(T, -cam_x, -cam_y, -cam_z);
mat4_mul(MV, g_rotMat, T);  // rotMat from quaternion, updated each physics step
glUniformMatrix4fv(u_mv, 1, GL_FALSE, MV);
```

**GL state that still works with shaders (keep these):**
- glEnable/Disable(GL_DEPTH_TEST)
- glEnable/Disable(GL_BLEND), glBlendFunc
- glEnable/Disable(GL_CULL_FACE)
- glClearColor, glClear, glViewport
- Texture binding: glBindTexture, glActiveTexture

**GL state that does nothing with shaders (harmless but useless):**
- glEnable(GL_LIGHTING), glLightfv, glMaterialf
- glEnable(GL_FOG), glFogf
- glTexGeni, glEnable(GL_TEXTURE_GEN_*)
- glShadeModel

### Step 3: GPU instancing (when you have many repeated objects)

**Use when:** same geometry is drawn at N different positions per frame,
N > ~20, and the geometry is non-trivial. Reduces N×20 draw calls to 20.

**Pattern:**
```cpp
// Instance data: one vec3 per instance (world-space translation)
// Pack all instances of type T into a contiguous range in g_inst_vbo
// Upload each frame:
glBindBuffer(GL_ARRAY_BUFFER, g_inst_vbo);
glBufferData(GL_ARRAY_BUFFER, total_instances * 12, data, GL_DYNAMIC_DRAW);

// Attrib setup:
glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, 12, offset_for_type_T);
glVertexAttribDivisor(4, 1);  // advance once per instance

// Draw:
glDrawElementsInstanced(GL_TRIANGLES, index_count, GL_UNSIGNED_INT,
                        ibo_offset, instance_count);
```

**Load glVertexAttribDivisor with ARB fallback** (required for GL 3.1 without 3.3):
```cpp
fn_VertexAttribDivisor = eglGetProcAddress("glVertexAttribDivisor");
if (!fn_VertexAttribDivisor)
    fn_VertexAttribDivisor = eglGetProcAddress("glVertexAttribDivisorARB");
// same for glDrawElementsInstanced / glDrawElementsInstancedARB
```

**Two-pass instance collection pattern** (avoids duplicating frustum loop):
```cpp
// Pass 1: collect visible cells into flat array
struct CellRef { short i, j, k, type; };
static CellRef vis[MAX_VIS];
int nvis = 0;
// ... frustum loop: vis[nvis++] = {i,j,k,type} ...

// Count per type, compute offsets
int type_cnt[NTYPES]={}, type_start[NTYPES+1];
for (int v=0;v<nvis;v++) type_cnt[vis[v].type]++;
type_start[0]=0;
for (int t=0;t<NTYPES;t++) type_start[t+1]=type_start[t]+type_cnt[t];

// Fill instance translation buffer
float inst[MAX_VIS][3]; int fill[NTYPES]={};
for (int v=0;v<nvis;v++) {
    int t=vis[v].type, idx=type_start[t]+fill[t]++;
    inst[idx][0]=(float)vis[v].i; inst[idx][1]=(float)vis[v].j; inst[idx][2]=(float)vis[v].k;
}
```

### Step 4: Index buffers (IBO) — always use for shared-vertex meshes

See Step 1. The rule: if the same geometric vertex appears in multiple
triangles with the same normal, emit it once and index it.

**For a smooth torus (lon×lat):**
- Vertex count: lon×lat (unique)
- Index count: lon×lat×6 (2 triangles per quad, 3 indices each)
- Speedup: 6× fewer vertex shader invocations via Post-Transform Cache

**Winding for torus quads** (verified against original RSS code):
```
for i in 0..lat-1, j in 0..lon-1:
  r0c0 = vbase + i*lon + j
  r0c1 = vbase + i*lon + (j+1)%lon
  r1c0 = vbase + ((i+1)%lat)*lon + j
  r1c1 = vbase + ((i+1)%lat)*lon + (j+1)%lon
  emit: r1c0, r0c0, r1c1   // triangle 1
  emit: r1c1, r0c0, r0c1   // triangle 2
```

---

## Matrix helpers (column-major, OpenGL convention)

These are needed because glMatrixMode/glTranslatef etc. are gone with shaders.
Copy from lattice.cpp:
- `mat4_identity(M)`
- `mat4_translate(M, tx, ty, tz)`
- `mat4_rotate(M, deg, ax, ay, az)`
- `mat4_mul(C, A, B)` — C = A × B, note: column-major multiply

---

## Frustum culling

Copy the `camera` class from lattice.cpp (~25 lines). It builds 4 half-plane
normals from the projection matrix and tests spheres against them.
Usage: `theCamera->inViewVolume(eye_pos_vec3, radius)`.

---

## FPS telemetry (keep during development)

```cpp
static struct { struct timespec t0; int frames; double ms_sum; } g_perf;
// in render_frame(), after render_scene():
//   accumulate frame_ms, print once per second:
fprintf(stderr, "FPS: %5.1f  frame: %5.2f ms\n", fps, avg_ms);
```

---

## Panfrost / Mali-T860 specific notes

- **Display lists:** DO NOT USE. 10–50× slower than VBOs due to software emulation.
- **GLSL version:** use `#version 140` (GL 3.1). Do not use 3.3+ features.
- **Geometry shaders:** not available on panfrost/T860 in practice.
- **Instancing:** `glVertexAttribDivisor` is GL 3.3 core but available via
  `GL_ARB_instanced_arrays` extension on GL 3.1. Always try both names.
- **Performance budget at 60fps:** ~16.7ms/frame. Render work should be ≤5ms
  to leave headroom for compositor. Target: ≤2ms for scenes like lattice.
- **Achieved results on RK3399/panfrost after full optimization:**
  - Lattice (brassmesh, ~500 visible cells, 4×4 tori): 60fps @ 1.6ms
  - Lattice (chainmail, ~500 visible cells, 24×12 tori): 57–59fps @ 2.1ms

---

## Texture handling

Original RSS textures live in `texture.h` as raw byte arrays. They load fine
with `gluBuild2DMipmaps`. Keep using them.

**GL_ALPHA format textures** (ghostly, circuits, etc.) return `(0,0,0,A)` from
`texture()` in GLSL — NOT `(1,1,1,A)`. Use `u_tex=3` (alpha-only modulate)
for these, not u_tex=1. Otherwise RGB is zeroed.

**Decal textures** (GL_DECAL in original): implement as u_tex=2 in fragment
shader — `mix(fragment_color, tex.rgb, tex.a)`.

---

## Keyboard / preset switching

Copy the keyboard handler from lattice.cpp. Key codes (Linux evdev):
- 1=Esc, 16=Q, 33=F, 87=F11, 105=Left arrow, 106=Right arrow

Preset switching pattern: `applyPreset(idx)` tears down GL visual state
(textures, VBOs, IBOs) and rebuilds for the new preset, without resetting
camera/physics state so the transition is seamless.

---

## What NOT to port from the original

- xscreensaver integration (replace with standalone Wayland window)
- Windows registry settings (replace with struct Preset + CLI args)
- rsVec/rsQuat/rsTimer classes (port inline, they're ~100 lines each)
- `rsMath.h` macros (just use cosf/sinf/sqrtf directly)
- Display list initialization (replace with buildXxxVBOs() + buildTorusCPU())
- glBegin/glEnd geometry (replace with VBO builder functions)

---

## Porting checklist

- [ ] Copy Wayland/EGL scaffolding from lattice.cpp verbatim
- [ ] Copy rsVec, rsQuat, rsTimer from lattice.cpp
- [ ] Copy mat4_* helpers, camera class, myMod, interpolate from lattice.cpp
- [ ] Port preset struct + CLI argument parsing
- [ ] Port physics/camera update logic (usually the draw() function's first half)
- [ ] Write buildXxxVBOs() to replace glNewList blocks:
  - [ ] Emit GVertex structs into std::vector
  - [ ] Emit unsigned int indices into std::vector (IBO)
  - [ ] Bake transforms into positions/normals
  - [ ] Upload VBO + IBO as GL_STATIC_DRAW
- [ ] Write GLSL vertex + fragment shaders (use templates above)
- [ ] Write set_shader_uniforms() for preset-dependent state
- [ ] Write render_scene() using glDrawElementsInstanced
- [ ] If N>20 repeated objects: add per-frame instance VBO
- [ ] Add FPS telemetry
- [ ] Verify all presets at 60fps
- [ ] Remove FPS telemetry (or make it --debug flag)

---

## File layout of a finished port

```
screensaver-wayland/
  lattice.cpp               ← everything in one file
  texture.h                 ← original RSS texture arrays, unmodified
  Makefile                  ← copy from lattice-wayland, change TARGET
  xdg-shell-client-protocol.h      ← generated by wayland-scanner
  xdg-shell-protocol.c             ← generated
  xdg-decoration-client-protocol.h ← generated
  xdg-decoration-protocol.c        ← generated
```

Single-file C++ is intentional. The RSS originals are single-file. Keeping
everything in one file makes it easy to read the complete rendering pipeline
top to bottom.

---

## The complete optimization impact (measured on RK3399/panfrost)

| Change | Mechanism | Typical speedup |
|--------|-----------|-----------------|
| Display lists → VBO | Eliminates software GL bytecode replay | 2–10× |
| Fixed-function → GLSL | Eliminates Mesa compat layer overhead | 1.5–3× |
| N×draw calls → instancing | Reduces CPU→GPU command overhead | 5–50× |
| Expanded triangles → IBO | Post-Transform Cache vertex reuse | 1.5–6× |

Apply all four. The order matters: VBO first (biggest risk reduction), then
shaders (required for instancing), then instancing, then IBO.
