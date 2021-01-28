#version 330 core

in VS_OUT {
    vec2 tex_coord;
} fs_in;

uniform sampler2D texture0;
uniform sampler2D texture1;
uniform float t;

out vec4 fragColor;

void main() {
    fragColor = (1.0f-t) * texture(texture0, fs_in.tex_coord) + t * texture(texture1, fs_in.tex_coord);
}
