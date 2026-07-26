import sys

with open('substrate.cpp', 'r') as f:
    lines = f.readlines()

def replace_block(start_marker, end_marker, replacement):
    start_idx = -1
    end_idx = -1
    for i, line in enumerate(lines):
        if start_marker in line and start_idx == -1:
            start_idx = i
        if start_idx != -1 and end_marker in line and end_idx == -1:
            end_idx = i
            break
    if start_idx == -1 or end_idx == -1:
        print(f"Error finding block: '{start_marker}' to '{end_marker}'")
        sys.exit(1)
    
    del lines[start_idx:end_idx+1]
    lines.insert(start_idx, replacement + "\n")

# 1. Globals
replace_block(
    "static PFN_GenBuffers_t    fn_GenBuffers    = nullptr;",
    "static PFN_DeleteBuffers_t fn_DeleteBuffers = nullptr;",
"""static PFN_GenBuffers_t    fn_GenBuffers    = nullptr;
static PFN_BindBuffer_t    fn_BindBuffer    = nullptr;
static PFN_BufferData_t    fn_BufferData    = nullptr;
static PFN_DeleteBuffers_t fn_DeleteBuffers = nullptr;

static PFNGLGENFRAMEBUFFERSPROC fn_GenFramebuffers = nullptr;
static PFNGLBINDFRAMEBUFFERPROC fn_BindFramebuffer = nullptr;
static PFNGLFRAMEBUFFERTEXTURE2DPROC fn_FramebufferTexture2D = nullptr;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC fn_CheckFramebufferStatus = nullptr;
static PFNGLDELETEFRAMEBUFFERSPROC fn_DeleteFramebuffers = nullptr;
typedef void (APIENTRY * PFNGLUNIFORM2FPROC) (GLint location, GLfloat v0, GLfloat v1);
static PFNGLUNIFORM2FPROC fn_Uniform2f = nullptr;

static GLuint g_fbo = 0;
static GLuint g_point_prog = 0;
static GLuint g_point_vbo = 0;
#include <vector>
static std::vector<float> g_point_buffer;"""
)

# 2. load_gl_functions
replace_block(
    "fn_DeleteBuffers = (PFN_DeleteBuffers_t)eglGetProcAddress(\"glDeleteBuffers\");",
    "fn_DeleteBuffers = (PFN_DeleteBuffers_t)eglGetProcAddress(\"glDeleteBuffers\");",
"""    fn_DeleteBuffers = (PFN_DeleteBuffers_t)eglGetProcAddress("glDeleteBuffers");
    fn_GenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)eglGetProcAddress("glGenFramebuffers");
    fn_BindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)eglGetProcAddress("glBindFramebuffer");
    fn_FramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)eglGetProcAddress("glFramebufferTexture2D");
    fn_CheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)eglGetProcAddress("glCheckFramebufferStatus");
    fn_DeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)eglGetProcAddress("glDeleteFramebuffers");
    fn_Uniform2f = (PFNGLUNIFORM2FPROC)eglGetProcAddress("glUniform2f");"""
)

# 3. mark_dirty and trans_point
replace_block(
    "static int dirty_min_x = 0;",
    "return f->bgcolor;\n}",
"""static inline uint32_t trans_point(int x, int y, uint32_t myc, float a, field *f) {
    if (x >= 0 && x < (int)f->width && y >= 0 && y < (int)f->height) {
        int r, g, b;
        point2rgb(myc, &r, &g, &b);
        g_point_buffer.push_back((float)x);
        g_point_buffer.push_back((float)y);
        g_point_buffer.push_back(r / 255.0f);
        g_point_buffer.push_back(g / 255.0f);
        g_point_buffer.push_back(b / 255.0f);
        g_point_buffer.push_back(a);
    }
    return myc;
}"""
)

# 4. movedrawcrack assignment
replace_block(
    "mark_dirty(cx, cy);",
    "f->off_img[cy * f->width + cx] = f->fgcolor;",
"""        int r, g, b;
        point2rgb(f->fgcolor, &r, &g, &b);
        g_point_buffer.push_back((float)cx);
        g_point_buffer.push_back((float)cy);
        g_point_buffer.push_back(r / 255.0f);
        g_point_buffer.push_back(g / 255.0f);
        g_point_buffer.push_back(b / 255.0f);
        g_point_buffer.push_back(1.0f);"""
)

# 5. clear_img
replace_block(
    "static void clear_img(struct field *f) {",
    "f->off_img[i] = f->bgcolor;",
"""static void clear_img(struct field *f) {
    if (g_fbo == 0) return;
    int r, g, b;
    point2rgb(f->bgcolor, &r, &g, &b);
    fn_BindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glClearColor(r/255.0f, g/255.0f, b/255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    fn_BindFramebuffer(GL_FRAMEBUFFER, 0);"""
)

# 6. setup_fbo & point shader
replace_block(
    "static void build_shader_program() {",
    "static void build_shader_program() {",
"""static void setup_fbo(int w, int h) {
    if (!g_fbo) fn_GenFramebuffers(1, &g_fbo);
    fn_BindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    fn_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_tex_id, 0);
    fn_BindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void build_point_shader_program() {
    const char *VERT_SRC =
        "#version 140\\n"
        "in vec2 a_pos;\\n"
        "in vec4 a_color;\\n"
        "out vec4 v_color;\\n"
        "uniform vec2 u_res;\\n"
        "void main() {\\n"
        "    vec2 ndc = (a_pos + 0.5) / u_res * 2.0 - 1.0;\\n"
        "    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\\n"
        "    v_color = a_color;\\n"
        "}\\n";

    const char *FRAG_SRC =
        "#version 140\\n"
        "in vec4 v_color;\\n"
        "out vec4 frag;\\n"
        "void main() {\\n"
        "    frag = v_color;\\n"
        "}\\n";

    auto compile = [](GLenum type, const char *src) -> GLuint {
        GLuint s = fn_CreateShader(type);
        fn_ShaderSource(s, 1, &src, nullptr);
        fn_CompileShader(s);
        return s;
    };
    GLuint vs = compile(GL_VERTEX_SHADER,   VERT_SRC);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FRAG_SRC);
    g_point_prog = fn_CreateProgram();
    fn_AttachShader(g_point_prog, vs);
    fn_AttachShader(g_point_prog, fs);
    fn_BindAttribLocation(g_point_prog, 0, "a_pos");
    fn_BindAttribLocation(g_point_prog, 1, "a_color");
    fn_LinkProgram(g_point_prog);
    fn_DeleteShader(vs);
    fn_DeleteShader(fs);
    if (!g_point_vbo) fn_GenBuffers(1, &g_point_vbo);
}

static void flush_points() {
    if (g_point_buffer.empty()) return;
    fn_BindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    fn_UseProgram(g_point_prog);
    if (fn_Uniform2f) {
        GLint res_loc = fn_GetUniformLocation(g_point_prog, "u_res");
        fn_Uniform2f(res_loc, (float)g_field->width, (float)g_field->height);
    }
    
    fn_BindBuffer(GL_ARRAY_BUFFER, g_point_vbo);
    fn_BufferData(GL_ARRAY_BUFFER, g_point_buffer.size() * sizeof(float), g_point_buffer.data(), GL_STREAM_DRAW);
    
    fn_EnableVertexAttribArray(0);
    fn_EnableVertexAttribArray(1);
    fn_VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    fn_VertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
    
    glDrawArrays(GL_POINTS, 0, g_point_buffer.size() / 6);
    
    fn_DisableVertexAttribArray(0);
    fn_DisableVertexAttribArray(1);
    fn_UseProgram(0);
    fn_BindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_BLEND);
    
    g_point_buffer.clear();
}

static void build_shader_program() {"""
)

# 7. initSaver reorder
replace_block(
    "build_substrate(g_field);",
    "update_cursor_color();\n}",
"""    setup_quad_geometry();
    setup_texture(g_field->width, g_field->height);
    setup_fbo(g_field->width, g_field->height);
    build_shader_program();
    build_point_shader_program();
    glViewport(0, 0, g_field->width, g_field->height);
    
    build_substrate(g_field);
    clear_img(g_field);
    update_cursor_color();
}"""
)

# 8. handle_resize reorder
replace_block(
    "build_substrate(g_field);",
    "glViewport(0, 0, w, h);\n}",
"""    setup_texture(w, h);
    setup_fbo(w, h);
    glViewport(0, 0, w, h);
    
    build_substrate(g_field);
    clear_img(g_field);
}"""
)

# 9. update_physics and flush
replace_block(
    "g_field->cycles++;",
    "g_field->cycles++;",
"""    g_field->cycles++;
    flush_points();"""
)

# 10. render_scene
replace_block(
    "static void render_scene() {",
    "dirty_max_x = -1;\n    }",
"""static void render_scene() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindTexture(GL_TEXTURE_2D, g_tex_id);"""
)

# 11. cleanup
replace_block(
    "if (g_prog) fn_DeleteProgram(g_prog);",
    "if (g_tex_id) glDeleteTextures(1, &g_tex_id);",
"""    if (g_prog) fn_DeleteProgram(g_prog);
    if (g_point_prog) fn_DeleteProgram(g_point_prog);
    if (g_vbo) fn_DeleteBuffers(1, &g_vbo);
    if (g_point_vbo) fn_DeleteBuffers(1, &g_point_vbo);
    if (g_ibo) fn_DeleteBuffers(1, &g_ibo);
    if (g_fbo) fn_DeleteFramebuffers(1, &g_fbo);
    if (g_tex_id) glDeleteTextures(1, &g_tex_id);"""
)

with open('substrate.cpp', 'w') as f:
    f.write("".join(lines))

print("Patched.")
