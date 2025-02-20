// vertex_shader.glsl
#version 330 core
layout(location = 0) in vec3 aPos;
out vec2 TexCoords;
void main() {
    // Convert from clip space to texture coordinates [0,1]
    TexCoords = (aPos.xy + vec2(1.0)) * 0.5;
    gl_Position = vec4(aPos, 1.0);
}
