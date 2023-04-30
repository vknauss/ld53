#include "opengl_utils.hpp"

#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

GLuint loadTexture(const char* filename)
{
    int width, height, components;
    stbi_uc* pixelData = stbi_load(filename, &width, &height, &components, STBI_rgb_alpha);
    if (!pixelData)
    {
        throw std::runtime_error("Failed to load texture: " + std::string(filename));
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixelData);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    stbi_image_free(pixelData);

    return texture;
}

GLuint loadShader(const char* filename, GLenum shaderType)
{
    std::ifstream fileStream(filename);
    if (!fileStream)
    {
        throw std::runtime_error("Failed to open file: " + std::string(filename));
    }

    const std::string fileAsString{ std::istreambuf_iterator<char>(fileStream), std::istreambuf_iterator<char>() };
    const char* source = fileAsString.c_str();
    GLuint shader = glCreateShader(shaderType);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compileStatus;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
    if (!compileStatus)
    {
        GLint infoLogLength;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);
        std::vector<char> infoLog(infoLogLength);
        glGetShaderInfoLog(shader, infoLogLength, nullptr, infoLog.data());
        throw std::runtime_error("Failed to compile shader: " + std::string(filename) + " Info log: " + std::string(infoLog.data()));
    }

    return shader;
}

GLuint createShaderProgram(const std::vector<GLuint>& shaders)
{
    GLuint program = glCreateProgram();

    for (const GLuint shader : shaders)
    {
        glAttachShader(program, shader);
    }

    glLinkProgram(program);

    for (const GLuint shader : shaders)
    {
        glDetachShader(program, shader);
    }

    GLint linkStatus;
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (!linkStatus)
    {
        GLint infoLogLength;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength);
        std::vector<char> infoLog(infoLogLength);
        glGetProgramInfoLog(program, infoLogLength, nullptr, infoLog.data());
        throw std::runtime_error("Failed to link program. Info log: " + std::string(infoLog.data()));
    }

    return program;
}
