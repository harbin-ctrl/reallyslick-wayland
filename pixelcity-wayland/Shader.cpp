#include "Shader.h"
#include "gl_ext.h"
#include <stdio.h>
#include <stdlib.h>

CShader::CShader(const char* vertexSource, const char* fragmentSource) {
    program = 0;
    if (!glCreateProgram) return;

    GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);

    program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        printf("ERROR::SHADER::PROGRAM::LINKING_FAILED\n%s\n", infoLog);
    }
}

CShader::~CShader() {
    // We leak the program since we don't have glDeleteProgram loaded, but it's fine for a singleton.
}

GLuint CShader::CompileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        printf("ERROR::SHADER::COMPILATION_FAILED\n%s\n", infoLog);
    }
    return shader;
}

void CShader::Bind() {
    if (glUseProgram) glUseProgram(program);
}

void CShader::Unbind() {
    if (glUseProgram) glUseProgram(0);
}

void CShader::SetUniform1i(const char* name, int value) {
    if (glGetUniformLocation) {
        glUniform1i(glGetUniformLocation(program, name), value);
    }
}

void CShader::SetUniform1f(const char* name, float value) {
    if (glGetUniformLocation) {
        glUniform1f(glGetUniformLocation(program, name), value);
    }
}

void CShader::SetUniform2f(const char* name, float x, float y) {
    if (glGetUniformLocation) {
        glUniform2f(glGetUniformLocation(program, name), x, y);
    }
}
