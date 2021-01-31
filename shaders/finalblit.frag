#version 330 core

in VS_OUT {
    vec2 tex_coord;
} fs_in;

uniform sampler2D texture0;
uniform sampler2D texture1;
uniform float t;

out vec4 fragColor;

void main() {
    vec4 p0 = texture(texture0, fs_in.tex_coord);
    vec4 p1 = texture(texture1, fs_in.tex_coord);
    float alpha = p0.a * p1.a;
    float t_final = t * p1.a;
    fragColor = vec4((1.0f-t_final) * p0.rgb + t_final * p1.rgb, alpha);
}
