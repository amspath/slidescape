#version 330

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex_coord;

out VS_OUT {
    vec2 tex_coord;
} vs_out;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    gl_Position = projection * view * model * vec4(pos.x, pos.y, pos.z, 1.0);
    vs_out.tex_coord = tex_coord;
}
