#version 330

in VS_OUT {
    vec2 tex_coord;
} fs_in;

uniform sampler2D the_texture;

void main() {
    vec4 the_texture_rgba = texture(the_texture, fs_in.tex_coord);
    float opacity = the_texture_rgba.a;

    gl_FragColor = vec4(opacity * the_texture_rgba.rgb + (1.0f-opacity) * vec3(0.85f, 0.85f, 0.85f), opacity);
}
