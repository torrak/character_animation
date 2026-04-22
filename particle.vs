#version 330 core
layout(location = 0) in vec4 inPosSize; // xyz, size (size is in clip canvas scale)
uniform mat4 projection;
uniform mat4 view;
out float vLifeSize;
void main()
{
    vec3 pos = inPosSize.xyz;
    float size = inPosSize.w;
    vLifeSize = size;
    gl_Position = projection * view * vec4(pos, 1.0);
    gl_PointSize = size; // size should be tuned in CPU
}