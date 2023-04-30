#pragma once

#include <vector>
#include <glad/glad.h>

GLuint loadTexture(const char* filename);
GLuint loadShader(const char* filename, GLenum shaderType);
GLuint createShaderProgram(const std::vector<GLuint>& shaders);
