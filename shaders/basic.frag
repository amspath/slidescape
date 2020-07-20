#version 140


in vec2 vs_tex_coord;


uniform vec3 bg_color;
uniform sampler2D the_texture;
uniform float black_level;
uniform float white_level;

void main() {
    vec4 the_texture_rgba = texture(the_texture, vs_tex_coord);

    float opacity = the_texture_rgba.a;
    vec3 color = the_texture_rgba.rgb;
    color = (color - black_level) * (1.0f / (white_level - black_level));

    gl_FragColor = vec4(opacity * color + (1.0f-opacity) * bg_color, opacity);
}
