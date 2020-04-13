#pragma once

// In imgui: ImDrawIdx
typedef unsigned short draw_index_t;

// In imgui: ImDrawVert
#pragma pack(push,0)
typedef struct {
	v2f pos;
	v2f uv;
	u32 col; // packed color
} draw_vertex_t;
#pragma pack(pop)

// Typically, 1 command = 1 GPU draw call (unless command is a callback)
// Pre 1.71 back-ends will typically ignore the VtxOffset/IdxOffset fields. When 'io.BackendFlags & ImGuiBackendFlags_RendererHasVtxOffset'
// is enabled, those fields allow us to render meshes larger than 64K vertices while keeping 16-bit indices.
typedef struct
{
	unsigned int    ElemCount;              // Number of indices (multiple of 3) to be rendered as triangles. Vertices are stored in the callee ImDrawList's vtx_buffer[] array, indices in idx_buffer[].
	v4f          ClipRect;               // Clipping rectangle (x1, y1, x2, y2). Subtract ImDrawData->DisplayPos to get clipping rectangle in "viewport" coordinates
	void*     TextureId;              // User-provided texture ID. Set by user in ImfontAtlas::SetTexID() for fonts or passed to Image*() functions. Ignore if never using images or multiple fonts atlas.
	unsigned int    VtxOffset;              // Start offset in vertex buffer. Pre-1.71 or without ImGuiBackendFlags_RendererHasVtxOffset: always 0. With ImGuiBackendFlags_RendererHasVtxOffset: may be >0 to support meshes larger than 64K vertices with 16-bit indices.
	unsigned int    IdxOffset;              // Start offset in index buffer. Always equal to sum of ElemCount drawn so far.
//	ImDrawCallback  UserCallback;           // If != NULL, call the function instead of rendering the vertices. clip_rect and texture_id will be set normally.
	void*           UserCallbackData;       // The draw callback code can access this.
} draw_cmd_t;

// Draw command list
// This is the low-level list of polygons that ImGui:: functions are filling. At the end of the frame,
// all command lists are passed to your ImGuiIO::RenderDrawListFn function for rendering.
// Each dear imgui window contains its own ImDrawList. You can use ImGui::GetWindowDrawList() to
// access the current window draw list and draw custom primitives.
// You can interleave normal ImGui:: calls and adding primitives to the current draw list.
// All positions are generally in pixel coordinates (generally top-left at 0,0, bottom-right at io.DisplaySize, unless multiple viewports are used), but you are totally free to apply whatever transformation matrix to want to the data (if you apply such transformation you'll want to apply it to ClipRect as well)
// Important: Primitives are always added to the list and not culled (culling is done at higher-level by ImGui:: functions), if you use this API a lot consider coarse culling your drawn objects.
typedef struct draw_list_t
{
	// This is what you have to render
	draw_cmd_t*     CmdBuffer;  //sb        // Draw commands. Typically 1 command = 1 GPU draw call, unless the command is a callback.
	draw_index_t*     IdxBuffer;          // Index buffer. Each command consume ImDrawCmd::ElemCount of those
	draw_vertex_t*    VtxBuffer;          // Vertex buffer.
	//ImDrawListFlags         Flags;              // Flags, you may poke into these to adjust anti-aliasing settings per-primitive.

	// [Internal, used while building lists]
	//const ImDrawListSharedData* _Data;          // Pointer to shared draw data (you can use ImGui::GetDrawListSharedData() to get the one from current ImGui context)
	const char*             _OwnerName;         // Pointer to owner window's name for debugging
	unsigned int            _VtxCurrentOffset;  // [Internal] Always 0 unless 'Flags & ImDrawListFlags_AllowVtxOffset'.
	unsigned int            _VtxCurrentIdx;     // [Internal] Generally == VtxBuffer.Size unless we are past 64K vertices, in which case this gets reset to 0.
	draw_vertex_t*             _VtxWritePtr;       // [Internal] point within VtxBuffer.Data after each add command (to avoid using the ImVector<> operators too much)
	draw_index_t*              _IdxWritePtr;       // [Internal] point within IdxBuffer.Data after each add command (to avoid using the ImVector<> operators too much)
//	ImVector<ImVec4>        _ClipRectStack;     // [Internal]
//	ImVector<ImTextureID>   _TextureIdStack;    // [Internal]
//	ImVector<ImVec2>        _Path;              // [Internal] current path building
//	ImDrawListSplitter      _Splitter;          // [Internal] for channels api

} draw_list_t;


// All draw data to render a Dear ImGui frame
// (NB: the style and the naming convention here is a little inconsistent, we currently preserve them for backward compatibility purpose,
// as this is one of the oldest structure exposed by the library! Basically, ImDrawList == CmdList)
typedef struct {
//	bool            Valid;                  // Only valid after Render() is called and before the next NewFrame() is called.
	draw_list_t**    CmdLists;               // Array of ImDrawList* to render. The ImDrawList are owned by ImGuiContext and only pointed to from here.
	int             CmdListsCount;          // Number of ImDrawList* to render
	int             TotalIdxCount;          // For convenience, sum of all ImDrawList's IdxBuffer.Size
	int             TotalVtxCount;          // For convenience, sum of all ImDrawList's VtxBuffer.Size
	v2f          DisplayPos;             // Upper-left position of the viewport to render (== upper-left of the orthogonal projection matrix to use)
	v2f          DisplaySize;            // Size of the viewport to render (== io.DisplaySize for the main viewport) (DisplayPos + DisplaySize == lower-right of the orthogonal projection matrix to use)
	v2f          FramebufferScale;       // Amount of pixels for each unit of DisplaySize. Based on io.DisplayFramebufferScale. Generally (1,1) on normal display, (2,2) on OSX with Retina display.
//	ImGuiViewport*  OwnerViewport;          // Viewport carrying the ImDrawData instance, might be of use to the renderer (generally not).
} draw_data_t;

extern draw_data_t g_draw_data;
extern draw_list_t g_draw_list;

#include "common.h"