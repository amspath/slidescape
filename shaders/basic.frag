#version 330 core

in VS_OUT {
    vec2 tex_coord;
} fs_in;

uniform vec3 bg_color;
uniform sampler2D the_texture;
uniform float black_level;
uniform float white_level;

out vec4 fragColor;

void main() {
    vec4 the_texture_rgba = texture(the_texture, fs_in.tex_coord);

    float opacity = the_texture_rgba.a;
    vec3 color = the_texture_rgba.rgb;
    color = (color - black_level) * (1.0f / (white_level - black_level));

    fragColor = vec4(opacity * color + (1.0f-opacity) * bg_color, opacity);
}
