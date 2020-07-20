#version 140

in vec3 pos;
in vec2 tex_coord;

out vec2 vs_tex_coord;

uniform mat4 model_matrix;
uniform mat4 projection_view_matrix;

void main() {
    gl_Position = projection_view_matrix * model_matrix * vec4(pos.xyz, 1.0f);
    vs_tex_coord = tex_coord;
}
