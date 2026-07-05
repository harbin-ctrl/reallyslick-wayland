#ifndef SHADER_H
#define SHADER_H

#include <GL/gl.h>

class CShader {
public:
    CShader(const char* vertexSource, const char* fragmentSource);
    ~CShader();

    void Bind();
    void Unbind();

    void SetUniform1i(const char* name, int value);
    void SetUniform1f(const char* name, float value);
    void SetUniform2f(const char* name, float x, float y);

private:
    GLuint program;
    GLuint CompileShader(GLenum type, const char* source);
};

#endif
