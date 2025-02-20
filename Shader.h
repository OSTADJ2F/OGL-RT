#ifndef SHADER_H
#define SHADER_H

#include <GL/glew.h>
#include <string>

// Reads the contents of a file and returns it as a string.
std::string readFile(const char* filePath);

// Compiles a shader from source code.
GLuint compileShader(const char* source, GLenum shaderType);

// Creates a shader program from vertex and fragment shader files.
GLuint createShaderProgram(const char* vertexPath, const char* fragmentPath);

#endif  // SHADER_H
