#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <stdio.h>
int main() {
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    void *ptr = (void*)eglGetProcAddress("glGenFramebuffers");
    printf("ptr = %p\n", ptr);
    void *ext_ptr = (void*)eglGetProcAddress("glGenFramebuffersEXT");
    printf("ext_ptr = %p\n", ext_ptr);
    return 0;
}
