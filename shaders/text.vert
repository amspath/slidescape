#version 330

layout (location = 0) in vec4 coord;

out VS_OUT {
    vec2 texcoord;
} vs_out;

void main(void) {
    gl_Position = vec4(coord.xy, 0, 1);
    vs_out.texcoord = coord.zw;
}
