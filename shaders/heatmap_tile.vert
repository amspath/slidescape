#version 330 core

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex_coord;

out VS_OUT {
    vec2 tex_coord;
} vs_out;

uniform mat4 model_matrix;
uniform mat4 projection_view_matrix;

void main() {
    gl_Position = projection_view_matrix * model_matrix * vec4(pos.xyz, 1.0f);
    vs_out.tex_coord = tex_coord;
}
