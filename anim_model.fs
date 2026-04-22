#version 330 core
out vec4 FragColor;

in vec2 TexCoords;
in vec3 FragPos;
in vec3 Normal;

uniform sampler2D texture_diffuse1;
uniform vec3 lightDir = normalize(vec3(0.3, 1.0, 0.5)); // simple directional light
uniform vec3 viewPos;

void main()
{
    vec3 color = texture(texture_diffuse1, TexCoords).rgb;

    // If texture is missing many exporters return black; add a small ambient fallback
    vec3 base = color;
    if(length(base) < 0.001) {
        base = vec3(0.8, 0.8, 0.8); // fallback grey
    }

    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, normalize(-lightDir)), 0.0);
    vec3 ambient = 0.35 * base;
    vec3 diffuse = 0.65 * diff * base;

    FragColor = vec4(ambient + diffuse, 1.0);
}