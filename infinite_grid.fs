#version 330 core
out vec4 FragColor;
in vec2 TexCoord;

uniform mat4 invProjection;
uniform mat4 invView;
uniform vec3 camPos;

float grid(in vec3 p, float size) {
    vec2 gv = abs(fract(p.xz/size - 0.5) - 0.5);
    float line = min(gv.x, gv.y);
    return 1.0 - smoothstep(0.0, 0.02, line);
}

void main()
{
    // reconstruct view ray from NDC
    vec2 ndc = TexCoord * 2.0 - 1.0;
    vec4 clip = vec4(ndc, -1.0, 1.0);
    vec4 viewPos = invProjection * clip;
    viewPos /= viewPos.w;
    vec4 worldDir4 = invView * vec4(viewPos.xyz, 0.0);
    vec3 rayDir = normalize(worldDir4.xyz);

    // ray-plane intersection y=0
    float t = (0.0 - camPos.y) / rayDir.y;
    vec3 P = camPos + rayDir * t;

    // layered grids
    float g1 = grid(P, 0.5) * 0.9;
    float g2 = grid(P, 2.0) * 0.6;
    float g3 = grid(P, 10.0) * 0.35;

    float d = distance(camPos, P);
    float fade = pow(clamp(1.0 - (d / 80.0), 0.0, 1.0), 2.0);
    vec3 color = vec3(0.12) + vec3(0.6) * (g1 + g2 + g3) * fade;

    FragColor = vec4(color, 1.0);
}