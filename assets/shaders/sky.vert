#version 330 core

layout (location = 0) in vec3 aPosition;

void main() {
    gl_Position = vec4(aPosition.xy, 0.999, 1.0);
}
