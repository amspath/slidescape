#version 330 core

in VS_OUT {
    vec2 tex_coord;
} fs_in;

uniform vec3 bg_color;
uniform sampler2D the_texture;
uniform float black_level;
uniform float white_level;
uniform vec3 transparent_color;
uniform float transparent_tolerance;
uniform bool use_transparent_filter;

out vec4 fragColor;

void main() {
    vec4 the_texture_rgba = texture(the_texture, fs_in.tex_coord);

    float opacity = the_texture_rgba.a;
    vec3 color = the_texture_rgba.rgb;

    if (use_transparent_filter) {
        vec3 difference = transparent_color - color;
        float diff_sq = dot(difference, difference);
        if (diff_sq < transparent_tolerance) {
            float t = diff_sq / transparent_tolerance;
            t = max(0, t * 1.2f - 0.2f);
            opacity = t;
        }
    }
    color = (color - black_level) * (1.0f / (white_level - black_level));

    fragColor = vec4(opacity * color + (1.0f-opacity) * bg_color, opacity);
}
