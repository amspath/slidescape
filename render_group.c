#pragma once
#include "common.h"
#include "viewer.h"

#include "shader.h"
#include "stretchy_buffer.h"


static u32 vbo_rect;
static u32 ebo_rect;
static u32 vao_rect;
static bool32 rect_initialized;

u32 basic_shader;
i32 basic_shader_u_projection_view_matrix;
i32 basic_shader_u_model_matrix;
i32 basic_shader_u_tex;
i32 basic_shader_u_black_level;
i32 basic_shader_u_white_level;
i32 basic_shader_u_background_color;

u32 ui_shader;
i32 ui_shader_u_texture;
i32 ui_shader_u_projmtx;
i32 ui_shader_attrib_pos;
i32 ui_shader_attrib_uv;
i32 ui_shader_attrib_color;
u32 ui_vbo_handle;
u32 ui_elements_handle;
u32 ui_vao;


static bool32 opengl_stuff_initialized;

void init_draw_rect() {
	ASSERT(!rect_initialized);
	rect_initialized = true;

	// suppress NVidia OpenGL driver warnings about 'no defined base level' while no textures are loaded
	glDisable(GL_TEXTURE_2D);

	glGenVertexArrays(1, &vao_rect);
	glBindVertexArray(vao_rect);

	glGenBuffers(1, &vbo_rect);
	glGenBuffers(1, &ebo_rect);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_rect);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_rect);

	static float vertices[] = {
			0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // x, y, z,  u, v
			1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f, 1.0f,

//			0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
//			1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
			1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
	};
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	static u16 indices[] = {
			0,1,2,1,2,3,
	};
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

	u32 vertex_stride = 5 * sizeof(float);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vertex_stride, (void*)0); // position coordinates
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, vertex_stride, (void*)(3*sizeof(float))); // texture coordinates
	glEnableVertexAttribArray(1);

}

void draw_rect(u32 texture) {
	glUseProgram(basic_shader);
	glBindVertexArray(vao_rect);
	glUniform1i(basic_shader_u_tex, 0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
}


// todo: integrate ImGui
void opengl_setup_render_state(draw_data_t* draw_data, int fb_width, int fb_height, GLuint vertex_array_object) {
	// Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled, polygon fill
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_SCISSOR_TEST);
#ifdef GL_POLYGON_MODE
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif

	// Setup viewport, orthographic projection matrix
	// Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
	glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height);
	float L = draw_data->DisplayPos.x;
	float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
	float T = draw_data->DisplayPos.y;
	float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
	const float ortho_projection[4][4] =
			{
					{ 2.0f/(R-L),   0.0f,         0.0f,   0.0f },
					{ 0.0f,         2.0f/(T-B),   0.0f,   0.0f },
					{ 0.0f,         0.0f,        -1.0f,   0.0f },
					{ (R+L)/(L-R),  (T+B)/(B-T),  0.0f,   1.0f },
			};
	glUseProgram(ui_shader);
	glUniform1i(ui_shader_u_texture, 0);
	glUniformMatrix4fv(ui_shader_u_projmtx, 1, GL_FALSE, &ortho_projection[0][0]);
#ifdef GL_SAMPLER_BINDING
	glBindSampler(0, 0); // We use combined texture/sampler state. Applications using GL 3.3 may set that otherwise.
#endif

	(void)vertex_array_object;
#ifndef IMGUI_IMPL_OPENGL_ES2
	glBindVertexArray(vertex_array_object);
#endif

	// Bind vertex/index buffers and setup attributes for ImDrawVert
	glBindBuffer(GL_ARRAY_BUFFER, ui_vbo_handle);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ui_elements_handle);
	glEnableVertexAttribArray(ui_shader_attrib_pos);
	glEnableVertexAttribArray(ui_shader_attrib_uv);
	glEnableVertexAttribArray(ui_shader_attrib_color);
	glVertexAttribPointer(ui_shader_attrib_pos, 2, GL_FLOAT, GL_FALSE, sizeof(draw_vertex_t), (GLvoid*)offsetof(draw_vertex_t, pos));
	glVertexAttribPointer(ui_shader_attrib_uv, 2, GL_FLOAT, GL_FALSE, sizeof(draw_vertex_t), (GLvoid*)offsetof(draw_vertex_t, uv));
	glVertexAttribPointer(ui_shader_attrib_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(draw_vertex_t), (GLvoid*)offsetof(draw_vertex_t, col));
}

// todo: integrate ImGui
void opengl_render_draw_data(draw_data_t* draw_data) {
	// Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
	int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
	int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
	if (fb_width <= 0 || fb_height <= 0)
		return;

	// Backup GL state
	GLenum last_active_texture; glGetIntegerv(GL_ACTIVE_TEXTURE, (GLint*)&last_active_texture);
	glActiveTexture(GL_TEXTURE0);
	GLint last_program; glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
	GLint last_texture; glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
#ifdef GL_SAMPLER_BINDING
	GLint last_sampler; glGetIntegerv(GL_SAMPLER_BINDING, &last_sampler);
#endif
	GLint last_array_buffer; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
#ifndef IMGUI_IMPL_OPENGL_ES2
	GLint last_vertex_array_object; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array_object);
#endif
#ifdef GL_POLYGON_MODE
	GLint last_polygon_mode[2]; glGetIntegerv(GL_POLYGON_MODE, last_polygon_mode);
#endif
	GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);
	GLint last_scissor_box[4]; glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box);
	GLenum last_blend_src_rgb; glGetIntegerv(GL_BLEND_SRC_RGB, (GLint*)&last_blend_src_rgb);
	GLenum last_blend_dst_rgb; glGetIntegerv(GL_BLEND_DST_RGB, (GLint*)&last_blend_dst_rgb);
	GLenum last_blend_src_alpha; glGetIntegerv(GL_BLEND_SRC_ALPHA, (GLint*)&last_blend_src_alpha);
	GLenum last_blend_dst_alpha; glGetIntegerv(GL_BLEND_DST_ALPHA, (GLint*)&last_blend_dst_alpha);
	GLenum last_blend_equation_rgb; glGetIntegerv(GL_BLEND_EQUATION_RGB, (GLint*)&last_blend_equation_rgb);
	GLenum last_blend_equation_alpha; glGetIntegerv(GL_BLEND_EQUATION_ALPHA, (GLint*)&last_blend_equation_alpha);
	GLboolean last_enable_blend = glIsEnabled(GL_BLEND);
	GLboolean last_enable_cull_face = glIsEnabled(GL_CULL_FACE);
	GLboolean last_enable_depth_test = glIsEnabled(GL_DEPTH_TEST);
	GLboolean last_enable_scissor_test = glIsEnabled(GL_SCISSOR_TEST);
	bool clip_origin_lower_left = true;
#if defined(GL_CLIP_ORIGIN) && !defined(__APPLE__)
	GLenum last_clip_origin = 0; glGetIntegerv(GL_CLIP_ORIGIN, (GLint*)&last_clip_origin); // Support for GL 4.5's glClipControl(GL_UPPER_LEFT)
	if (last_clip_origin == GL_UPPER_LEFT)
		clip_origin_lower_left = false;
#endif

	// Setup desired GL state
	// Recreate the VAO every time (this is to easily allow multiple GL contexts to be rendered to. VAO are not shared among GL contexts)
	// The renderer would actually work without any VAO bound, but then our VertexAttrib calls would overwrite the default one currently bound.
	GLuint vertex_array_object = 0;
#ifndef IMGUI_IMPL_OPENGL_ES2
	glGenVertexArrays(1, &vertex_array_object);
#endif
	opengl_setup_render_state(draw_data, fb_width, fb_height, vertex_array_object);

	// Will project scissor/clipping rectangles into framebuffer space
	v2f clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
	v2f clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

	// Render command lists
	for (int n = 0; n < draw_data->CmdListsCount; n++)
	{
		const draw_list_t* cmd_list = draw_data->CmdLists[n];

		// Upload vertex/index buffers
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sb_count(cmd_list->VtxBuffer) * sizeof(draw_vertex_t), (const GLvoid*)cmd_list->VtxBuffer, GL_STREAM_DRAW);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)sb_count(cmd_list->IdxBuffer) * sizeof(draw_index_t), (const GLvoid*)cmd_list->IdxBuffer, GL_STREAM_DRAW);

		for (int cmd_i = 0; cmd_i < sb_count(cmd_list->CmdBuffer); cmd_i++)
		{
			const draw_cmd_t* pcmd = &cmd_list->CmdBuffer[cmd_i];
/*			if (pcmd->UserCallback != NULL)
			{
				// User callback, registered via ImDrawList::AddCallback()
				// (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
				if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
					ImGui_ImplOpenGL3_SetupRenderState(draw_data, fb_width, fb_height, vertex_array_object);
				else
					pcmd->UserCallback(cmd_list, pcmd);
			}
			else*/
			{
				// Project scissor/clipping rectangles into framebuffer space
				v4f clip_rect;
				clip_rect.x = (pcmd->ClipRect.x - clip_off.x) * clip_scale.x;
				clip_rect.y = (pcmd->ClipRect.y - clip_off.y) * clip_scale.y;
				clip_rect.z = (pcmd->ClipRect.z - clip_off.x) * clip_scale.x;
				clip_rect.w = (pcmd->ClipRect.w - clip_off.y) * clip_scale.y;

				if (clip_rect.x < fb_width && clip_rect.y < fb_height && clip_rect.z >= 0.0f && clip_rect.w >= 0.0f)
				{
					// Apply scissor/clipping rectangle
					if (clip_origin_lower_left)
						glScissor((int)clip_rect.x, (int)(fb_height - clip_rect.w), (int)(clip_rect.z - clip_rect.x), (int)(clip_rect.w - clip_rect.y));
					else
						glScissor((int)clip_rect.x, (int)clip_rect.y, (int)clip_rect.z, (int)clip_rect.w); // Support for GL 4.5 rarely used glClipControl(GL_UPPER_LEFT)

					// Bind texture, Draw
					glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->TextureId);
#if IMGUI_IMPL_OPENGL_MAY_HAVE_VTX_OFFSET
					if (g_GlVersion >= 3200)
                        glDrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(draw_index_t) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, (void*)(intptr_t)(pcmd->IdxOffset * sizeof(draw_index_t)), (GLint)pcmd->VtxOffset);
                    else
#endif
					glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, GL_UNSIGNED_SHORT, (void*)(intptr_t)(pcmd->IdxOffset * sizeof(draw_index_t)));
				}
			}
		}
	}

	// Destroy the temporary VAO
#ifndef IMGUI_IMPL_OPENGL_ES2
	glDeleteVertexArrays(1, &vertex_array_object);
#endif

	// Restore modified GL state
	glUseProgram(last_program);
	glBindTexture(GL_TEXTURE_2D, last_texture);
#ifdef GL_SAMPLER_BINDING
	glBindSampler(0, last_sampler);
#endif
	glActiveTexture(last_active_texture);
#ifndef IMGUI_IMPL_OPENGL_ES2
	glBindVertexArray(last_vertex_array_object);
#endif
	glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
	glBlendEquationSeparate(last_blend_equation_rgb, last_blend_equation_alpha);
	glBlendFuncSeparate(last_blend_src_rgb, last_blend_dst_rgb, last_blend_src_alpha, last_blend_dst_alpha);
	if (last_enable_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	if (last_enable_cull_face) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (last_enable_depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (last_enable_scissor_test) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
#ifdef GL_POLYGON_MODE
	glPolygonMode(GL_FRONT_AND_BACK, (GLenum)last_polygon_mode[0]);
#endif
	glViewport(last_viewport[0], last_viewport[1], (GLsizei)last_viewport[2], (GLsizei)last_viewport[3]);
	glScissor(last_scissor_box[0], last_scissor_box[1], (GLsizei)last_scissor_box[2], (GLsizei)last_scissor_box[3]);
}

draw_data_t g_draw_data;
draw_list_t g_draw_list;

// todo: integrate ImGui
void render_ui(draw_data_t* draw_data, image_t* image) {
	draw_list_t* draw_list = draw_data->CmdLists[0];

	draw_list->CmdBuffer = NULL; // sb
	draw_list->VtxBuffer = NULL; // sb
	draw_list->IdxBuffer = NULL; // sb

	draw_list->_VtxCurrentOffset = 0;
	draw_list->_VtxCurrentIdx = 0;

	// push a command onto the command buffer
	draw_cmd_t* cmd = sb_add(draw_list->CmdBuffer, 1);
	memset(cmd, 0, sizeof(*cmd));
	cmd->TextureId = (void *) image->stbi.texture;
	cmd->VtxOffset = sb_count(draw_list->VtxBuffer) * sizeof(draw_vertex_t);
	cmd->IdxOffset = sb_count(draw_list->IdxBuffer) * sizeof(draw_index_t);
	cmd->ClipRect = (v4f) {0, 0, 2000, 2000 };
	cmd->ElemCount = 6;

	// reserve space for primitives and fill them in
	// PrimRectUV - draw rectangle
	v2f a = {30, 50};
	v2f c = {200, 225};
	v2f b = {c.x, a.y};
	v2f d = {a.x, c.y};

	v2f uv_a = {0,0};
	v2f uv_c = {1,1};
	v2f uv_b = {uv_c.x, uv_a.y};
	v2f uv_d = {uv_a.x, uv_c.y};

	u32 color = 0xFFFFFFFF;

	draw_index_t idx = draw_list->_VtxCurrentIdx;
	draw_index_t* idx_write_ptr = sb_add(draw_list->IdxBuffer, 6);
	idx_write_ptr[0] = idx;
	idx_write_ptr[1] = idx+1;
	idx_write_ptr[2] = idx+2;
	idx_write_ptr[3] = idx;
	idx_write_ptr[4] = idx+2;
	idx_write_ptr[5] = idx+3;
	draw_vertex_t* vertex_write_ptr = sb_add(draw_list->VtxBuffer, 4);
	vertex_write_ptr[0] = (draw_vertex_t){.pos = a, .uv = uv_a, .col = color};
	vertex_write_ptr[1] = (draw_vertex_t){.pos = b, .uv = uv_b, .col = color};
	vertex_write_ptr[2] = (draw_vertex_t){.pos = c, .uv = uv_c, .col = color};
	vertex_write_ptr[3] = (draw_vertex_t){.pos = d, .uv = uv_d, .col = color};

	opengl_render_draw_data(draw_data);


	sb_free(draw_list->CmdBuffer);
	sb_free(draw_list->VtxBuffer);
	sb_free(draw_list->IdxBuffer);
}

void init_draw_data(draw_data_t* draw_data, i32 client_width, i32 client_height) {
	draw_list_t** lists = sb_add(draw_data->CmdLists, 1);
	lists[0] = &g_draw_list;
	draw_data->CmdListsCount = 1;
	draw_data->DisplayPos = (v2f) {0, 0};
	draw_data->DisplaySize = (v2f) {(float) client_width, (float) client_height};
	draw_data->FramebufferScale = (v2f) {1, 1};

}


void init_opengl_stuff(i32 client_width, i32 client_height) {
	ASSERT(!opengl_stuff_initialized);
	opengl_stuff_initialized = true;

	// TODO: don't depend on seperate shader text files in a release build
	// TODO: look in the executable directory
	basic_shader = load_basic_shader_program("shaders/basic.vert", "shaders/basic.frag");
	basic_shader_u_projection_view_matrix = get_uniform(basic_shader, "projection_view_matrix");
	basic_shader_u_model_matrix = get_uniform(basic_shader, "model_matrix");
	basic_shader_u_tex = get_uniform(basic_shader, "the_texture");
	basic_shader_u_black_level = get_uniform(basic_shader, "black_level");
	basic_shader_u_white_level = get_uniform(basic_shader, "white_level");
	basic_shader_u_background_color = get_uniform(basic_shader, "bg_color");

	ui_shader = load_basic_shader_program("shaders/ui.vert", "shaders/ui.frag");
	ui_shader_u_texture = get_uniform(ui_shader, "Texture");
	ui_shader_u_projmtx = get_uniform(ui_shader, "ProjMtx");
	ui_shader_attrib_pos = get_attrib(ui_shader, "Position");
	ui_shader_attrib_uv = get_attrib(ui_shader, "UV");
	ui_shader_attrib_color = get_attrib(ui_shader, "Color");

	// Create buffers
	glGenBuffers(1, &ui_vbo_handle);
	glGenBuffers(1, &ui_elements_handle);


#ifdef STRINGIFY_SHADERS
	write_stringified_shaders();
#endif
	glEnable(GL_TEXTURE_2D);

	init_draw_rect();

	init_draw_data(&g_draw_data, client_width, client_height);
}


typedef struct render_group_t {
	u32 max_pushbuffer_size;
	u32 pushbuffer_size;
	u8* pushbuffer_base;

} render_group_t;

typedef struct render_basis_t {

} render_basis_t;


render_group_t* allocate_render_group(arena_t* arena, u32 max_pushbuffer_size) {
	render_group_t* result = push_struct(arena, render_group_t);
	*result = (render_group_t) {
		.pushbuffer_base = (u8*) push_size(arena, max_pushbuffer_size),
	};
}

void* push_render_entry(render_group_t* group, u32 size) {
	void* result = NULL;
	if (group->pushbuffer_size + size < group->max_pushbuffer_size) {
		result = group->pushbuffer_base + group->pushbuffer_size;
	} else {
		panic();
	}
	return result;
}