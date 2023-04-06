#version 330 core

in VS_OUT {
    vec2 tex_coord;
} fs_in;

uniform float intensity;
//uniform sampler2D base_texture;
//uniform sampler2D color_ramp;
uniform float min_intensity;
uniform float max_opacity;

out vec4 fragColor;

void main() {
    float threshold = 0.3f;
    float intensity_transformed = clamp((intensity - 0.5f) * 2.0f, 0.0f, 1.0f);
    float opacity = mix(0.0f, max_opacity, intensity_transformed);
    fragColor = vec4(1.0f, 0.7f, 0.0f, opacity);
}
