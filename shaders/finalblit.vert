#version 330 core

layout (location = 0) in vec2 pos;
layout (location = 1) in vec2 tex_coord;

out VS_OUT {
    vec2 tex_coord;
} vs_out;

void main() {
    gl_Position = vec4(pos.xy, 0.0f, 1.0f);
    vs_out.tex_coord = tex_coord;
}
