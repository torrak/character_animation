#version 330 core
out vec4 FragColor;
in float vLifeSize;

void main()
{
    // make a soft ring using gl_PointCoord
    vec2 uv = gl_PointCoord - 0.5;
    float r = length(uv);
    float alpha = smoothstep(0.5, 0.45, r) * (vLifeSize > 0.0 ? clamp(vLifeSize/150.0, 0.0, 1.0) : 1.0);
    float ring = smoothstep(0.28, 0.3, r) - smoothstep(0.35, 0.36, r);
    vec3 col = vec3(0.9, 0.85, 0.75) * 1.0;
    FragColor = vec4(col, alpha * (ring + 0.25));
    if (FragColor.a < 0.01) discard;
}