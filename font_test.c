#define USE_MINIMAL_SYSTEM_HEADER
#include "common.h"
#include "platform.h"
#include "win32_platform.h"
#include "shader.h"

#include <glad/glad.h>

#include <ft2build.h>
#include <freetype/freetype.h>

#include <shlobj.h>

#if DO_DEBUG

// Font test code adapted from:
// https://en.wikibooks.org/wiki/OpenGL_Programming/Modern_OpenGL_Tutorial_Text_Rendering_01

char system_fonts_folder[MAX_PATH];
file_mem_t* system_font_buffer;

char* win32_get_system_fonts_folder() {
	HRESULT result = SHGetFolderPathA(NULL, CSIDL_FONTS, NULL, 0, system_fonts_folder);
	if (result != S_OK) {
		win32_diagnostic("SHGetFolderPathA");
		panic();
	}
	return system_fonts_folder;
}

FT_Face system_face;

void win32_init_font() {
	i64 debug_start = get_clock();

	char ttf_filename[2048];
	snprintf(ttf_filename, sizeof(ttf_filename), "%s\\segoeui.ttf", win32_get_system_fonts_folder());

	system_font_buffer = platform_read_entire_file(ttf_filename);
	if (!system_font_buffer) {
		printf("Error: could not load system font file %s\n", ttf_filename);
		panic();
	}

	FT_Library ft;

	if(FT_Init_FreeType(&ft)) {
		fprintf(stderr, "Could not init freetype library\n");
		panic();
	}



	if(FT_New_Face(ft, ttf_filename, 0, &system_face)) {
		fprintf(stderr, "Could not open font\n");
		panic();
	}

	FT_Set_Pixel_Sizes(system_face, 0, 48);

	if(FT_Load_Char(system_face, 'X', FT_LOAD_RENDER)) {
		fprintf(stderr, "Could not load character 'X'\n");
		panic();
	}

	printf("Initialized FreeType in %g seconds.\n", get_seconds_elapsed(debug_start, get_clock()));

}


////////////////////////////


typedef struct text_shader_t {
	u32 program;
	i32 attribute_coord;
	i32 uniform_tex;
	i32 uniform_color;
	u32 vbo;
	u32 vao;
} text_shader_t;

text_shader_t text_shader;

typedef struct text_point_t {
	float x;
	float y;
	float s;
	float t;
} text_point_t;


void render_text(const char* text, float x, float y, float sx, float sy) {


	const char *p;
	FT_GlyphSlot g = system_face->glyph;

	/* Create a texture that will be used to hold one "glyph" */
	GLuint tex;
	glActiveTexture(GL_TEXTURE0);
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glUniform1i(text_shader.uniform_tex, 0);

	/* We require 1 byte alignment when uploading texture data */
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	/* Clamping to edges is important to prevent artifacts when scaling */
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	/* Linear filtering usually looks best for text */
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	/* Set up the VBO for our vertex data */
	glEnableVertexAttribArray(text_shader.attribute_coord);
	glBindBuffer(GL_ARRAY_BUFFER, text_shader.vbo);
	glVertexAttribPointer(text_shader.attribute_coord, 4, GL_FLOAT, GL_FALSE, 0, 0);

	/* Loop through all characters */
	for (p = text; *p; p++) {
		/* Try to load and render the character */
		if (FT_Load_Char(system_face, *p, FT_LOAD_RENDER))
			continue;

		/* Upload the "bitmap", which contains an 8-bit grayscale image, as an alpha texture */
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, g->bitmap.width, g->bitmap.rows, 0, GL_RED, GL_UNSIGNED_BYTE, g->bitmap.buffer);

		/* Calculate the vertex and texture coordinates */
		float x2 = x + g->bitmap_left * sx;
		float y2 = -y - g->bitmap_top * sy;
		float w = g->bitmap.width * sx;
		float h = g->bitmap.rows * sy;

		text_point_t box[4] = {
				{x2, -y2, 0, 0},
				{x2 + w, -y2, 1, 0},
				{x2, -y2 - h, 0, 1},
				{x2 + w, -y2 - h, 1, 1},
		};

		/* Draw the character on the screen */
		glBufferData(GL_ARRAY_BUFFER, sizeof box, box, GL_DYNAMIC_DRAW);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		/* Advance the cursor to the start of the next character */
		x += (g->advance.x >> 6) * sx;
		y += (g->advance.y >> 6) * sy;
	}

	glDisableVertexAttribArray(text_shader.attribute_coord);
	glDeleteTextures(1, &tex);

}

void init_font_test_text_shader() {
	static bool32 initialized;
	ASSERT(!initialized);
	initialized = true;

	text_shader.program = load_basic_shader_program("shaders/text.vert", "shaders/text.frag");
	if (!text_shader.program) {
		printf("Error: could not load text shader\n");
		panic();
	}

	text_shader.attribute_coord = get_attrib(text_shader.program, "coord");
	text_shader.uniform_color = get_uniform(text_shader.program, "color");
	text_shader.uniform_tex = get_uniform(text_shader.program, "tex");

	glGenBuffers(1, &text_shader.vbo);


}


void text_test(i32 client_width, i32 client_height) {
	float sx = 2.0 / client_width;
	float sy = 2.0 / client_height;

	glUseProgram(text_shader.program);

	/* White background */
	glClearColor(1, 1, 1, 1);
	glClear(GL_COLOR_BUFFER_BIT);

	/* Enable blending, necessary for our alpha texture */
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	GLfloat black[4] = { 0, 0, 0, 1 };
	GLfloat red[4] = { 1, 0, 0, 1 };
	GLfloat transparent_green[4] = { 0, 1, 0, 0.5 };

	/* Set font size to 48 pixels, color to black */
	FT_Set_Pixel_Sizes(system_face, 0, 48);
	glUniform4fv(text_shader.uniform_color, 1, black);

	/* Effects of alignment */
	render_text("The Quick Brown Fox Jumps Over The Lazy Dog", -1 + 8 * sx, 1 - 50 * sy, sx, sy);
	render_text("The Misaligned Fox Jumps Over The Lazy Dog", -1 + 8.5 * sx, 1 - 100.5 * sy, sx, sy);

	/* Scaling the texture versus changing the font size */
	render_text("The Small Texture Scaled Fox Jumps Over The Lazy Dog", -1 + 8 * sx, 1 - 175 * sy, sx * 0.5, sy * 0.5);
	FT_Set_Pixel_Sizes(system_face, 0, 24);
	render_text("The Small Font Sized Fox Jumps Over The Lazy Dog", -1 + 8 * sx, 1 - 200 * sy, sx, sy);
	FT_Set_Pixel_Sizes(system_face, 0, 48);
	render_text("The Tiny Texture Scaled Fox Jumps Over The Lazy Dog", -1 + 8 * sx, 1 - 235 * sy, sx * 0.25, sy * 0.25);
	FT_Set_Pixel_Sizes(system_face, 0, 12);
	render_text("The Tiny Font Sized Fox Jumps Over The Lazy Dog", -1 + 8 * sx, 1 - 250 * sy, sx, sy);
	FT_Set_Pixel_Sizes(system_face, 0, 48);

	/* Colors and transparency */
	render_text("The Solid Black Fox Jumps Over The Lazy Dog", -1 + 8 * sx, 1 - 430 * sy, sx, sy);

	glUniform4fv(text_shader.uniform_color, 1, red);
	render_text("The Solid Red Fox Jumps Over The Lazy Dog", -1 + 8 * sx, 1 - 330 * sy, sx, sy);
	render_text("The Solid Red Fox Jumps Over The Lazy Dog", -1 + 28 * sx, 1 - 450 * sy, sx, sy);

	glUniform4fv(text_shader.uniform_color, 1, transparent_green);
	render_text("The Transparent Green Fox Jumps Over The Lazy Dog", -1 + 8 * sx, 1 - 380 * sy, sx, sy);
	render_text("The Transparent Green Fox Jumps Over The Lazy Dog", -1 + 18 * sx, 1 - 440 * sy, sx, sy);

}


#endif //DO_DEBUG
