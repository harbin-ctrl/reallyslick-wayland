#include "gl_ext.h"
#include <EGL/egl.h>
#include <stdio.h>

PFNGLGENFRAMEBUFFERSEXTPROC glGenFramebuffersEXT = NULL;
PFNGLBINDFRAMEBUFFEREXTPROC glBindFramebufferEXT = NULL;
PFNGLFRAMEBUFFERTEXTURE2DEXTPROC glFramebufferTexture2DEXT = NULL;
PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC glCheckFramebufferStatusEXT = NULL;

PFNGLCREATESHADERPROC glCreateShader = NULL;
PFNGLSHADERSOURCEPROC glShaderSource = NULL;
PFNGLCOMPILESHADERPROC glCompileShader = NULL;
PFNGLGETSHADERIVPROC glGetShaderiv = NULL;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = NULL;
PFNGLCREATEPROGRAMPROC glCreateProgram = NULL;
PFNGLATTACHSHADERPROC glAttachShader = NULL;
PFNGLLINKPROGRAMPROC glLinkProgram = NULL;
PFNGLGETPROGRAMIVPROC glGetProgramiv = NULL;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = NULL;
PFNGLUSEPROGRAMPROC glUseProgram = NULL;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = NULL;
PFNGLUNIFORM1IPROC glUniform1i = NULL;
PFNGLUNIFORM1FPROC glUniform1f = NULL;
PFNGLUNIFORM2FPROC glUniform2f = NULL;

PFNGLGENRENDERBUFFERSEXTPROC glGenRenderbuffersEXT = NULL;
PFNGLBINDRENDERBUFFEREXTPROC glBindRenderbufferEXT = NULL;
PFNGLRENDERBUFFERSTORAGEEXTPROC glRenderbufferStorageEXT = NULL;
PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC glFramebufferRenderbufferEXT = NULL;

void LoadGLExtensions() {
    glGenFramebuffersEXT = (PFNGLGENFRAMEBUFFERSEXTPROC)eglGetProcAddress("glGenFramebuffersEXT");
    if (!glGenFramebuffersEXT) glGenFramebuffersEXT = (PFNGLGENFRAMEBUFFERSEXTPROC)eglGetProcAddress("glGenFramebuffers");
    
    glBindFramebufferEXT = (PFNGLBINDFRAMEBUFFEREXTPROC)eglGetProcAddress("glBindFramebufferEXT");
    if (!glBindFramebufferEXT) glBindFramebufferEXT = (PFNGLBINDFRAMEBUFFEREXTPROC)eglGetProcAddress("glBindFramebuffer");

    glFramebufferTexture2DEXT = (PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)eglGetProcAddress("glFramebufferTexture2DEXT");
    if (!glFramebufferTexture2DEXT) glFramebufferTexture2DEXT = (PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)eglGetProcAddress("glFramebufferTexture2D");

    glCheckFramebufferStatusEXT = (PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)eglGetProcAddress("glCheckFramebufferStatusEXT");
    if (!glCheckFramebufferStatusEXT) glCheckFramebufferStatusEXT = (PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)eglGetProcAddress("glCheckFramebufferStatus");

    glGenRenderbuffersEXT = (PFNGLGENRENDERBUFFERSEXTPROC)eglGetProcAddress("glGenRenderbuffersEXT");
    if (!glGenRenderbuffersEXT) glGenRenderbuffersEXT = (PFNGLGENRENDERBUFFERSEXTPROC)eglGetProcAddress("glGenRenderbuffers");

    glBindRenderbufferEXT = (PFNGLBINDRENDERBUFFEREXTPROC)eglGetProcAddress("glBindRenderbufferEXT");
    if (!glBindRenderbufferEXT) glBindRenderbufferEXT = (PFNGLBINDRENDERBUFFEREXTPROC)eglGetProcAddress("glBindRenderbuffer");

    glRenderbufferStorageEXT = (PFNGLRENDERBUFFERSTORAGEEXTPROC)eglGetProcAddress("glRenderbufferStorageEXT");
    if (!glRenderbufferStorageEXT) glRenderbufferStorageEXT = (PFNGLRENDERBUFFERSTORAGEEXTPROC)eglGetProcAddress("glRenderbufferStorage");

    glFramebufferRenderbufferEXT = (PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC)eglGetProcAddress("glFramebufferRenderbufferEXT");
    if (!glFramebufferRenderbufferEXT) glFramebufferRenderbufferEXT = (PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC)eglGetProcAddress("glFramebufferRenderbuffer");

    glCreateShader = (PFNGLCREATESHADERPROC)eglGetProcAddress("glCreateShader");
    glShaderSource = (PFNGLSHADERSOURCEPROC)eglGetProcAddress("glShaderSource");
    glCompileShader = (PFNGLCOMPILESHADERPROC)eglGetProcAddress("glCompileShader");
    glGetShaderiv = (PFNGLGETSHADERIVPROC)eglGetProcAddress("glGetShaderiv");
    glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)eglGetProcAddress("glGetShaderInfoLog");
    glCreateProgram = (PFNGLCREATEPROGRAMPROC)eglGetProcAddress("glCreateProgram");
    glAttachShader = (PFNGLATTACHSHADERPROC)eglGetProcAddress("glAttachShader");
    glLinkProgram = (PFNGLLINKPROGRAMPROC)eglGetProcAddress("glLinkProgram");
    glGetProgramiv = (PFNGLGETPROGRAMIVPROC)eglGetProcAddress("glGetProgramiv");
    glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)eglGetProcAddress("glGetProgramInfoLog");
    glUseProgram = (PFNGLUSEPROGRAMPROC)eglGetProcAddress("glUseProgram");
    glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)eglGetProcAddress("glGetUniformLocation");
    glUniform1i = (PFNGLUNIFORM1IPROC)eglGetProcAddress("glUniform1i");
    glUniform1f = (PFNGLUNIFORM1FPROC)eglGetProcAddress("glUniform1f");
    glUniform2f = (PFNGLUNIFORM2FPROC)eglGetProcAddress("glUniform2f");

    if (!glCreateShader) {
        printf("WARNING: glCreateShader not found!\n");
    }
}
