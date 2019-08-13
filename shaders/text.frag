#version 330

in VS_OUT {
    vec2 texcoord;
} fs_in;

//varying vec2 texcoord;
uniform sampler2D tex;
uniform vec4 color;

void main(void) {
    float alpha = texture(tex, fs_in.texcoord).r;
    gl_FragColor = vec4(1, 1, 1, alpha) * color;
}
