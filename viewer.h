#pragma once
#include "common.h"


typedef struct texture_t {
	u32 texture;
	i32 width;
	i32 height;
} texture_t;

typedef struct rect2i {
	i32 x, y, w, h;
} rect2i;

typedef struct v2i {
	i32 x, y;
} v2i;

typedef struct v2f {
	float x, y;
} v2f;

typedef struct v3f {
	union {
		struct {float r, g, b; };
		struct {float x, y, z; };
	};
} v3f;

typedef struct v4f {
	union {
		struct {float r, g, b, a; };
		struct {float x, y, z, w; };
	};
} v4f;

typedef struct button_state_t {
	bool8 down;
	u8 transition_count;
} button_state_t;

typedef struct controller_input_t {
	bool32 is_connected;
	bool32 is_analog;
	float x_start, y_start;
	float x_min, y_min;
	float x_max, y_max;
	float x_end, y_end; // end state
	union {
		button_state_t buttons[16];
		struct {
			button_state_t move_up;
			button_state_t move_down;
			button_state_t move_left;
			button_state_t move_right;
			button_state_t action_up;
			button_state_t action_down;
			button_state_t action_left;
			button_state_t action_right;
			button_state_t left_shoulder;
			button_state_t right_shoulder;
			button_state_t start;
			button_state_t back;
			button_state_t button_a;
			button_state_t button_b;
			button_state_t button_x;
			button_state_t button_y;

			// NOTE: add buttons above this line
			// cl complains about zero-sized arrays, so this the terminator is a full blown button now :(
			button_state_t terminator;
		};
	};
	button_state_t keys[256];
} controller_input_t;

typedef struct {
	button_state_t mouse_buttons[5];
	i32 mouse_z;
	v2i dmouse_xy;
	v2i drag_start_xy;
	v2i drag_vector;
	v2i mouse_xy;
	float delta_t;
	union {
		controller_input_t abstract_controllers[5];
		struct {
			controller_input_t keyboard;
			controller_input_t controllers[4];
		};
	};

} input_t;

#define TILE_DIM 512
#define BYTES_PER_PIXEL 4
#define TILE_PITCH (TILE_DIM * BYTES_PER_PIXEL)
#define WSI_BLOCK_SIZE (TILE_DIM * TILE_DIM * BYTES_PER_PIXEL)

typedef struct {
	i64 capacity;
	u32 capacity_in_blocks;
	u32 blocks_in_use;
	u8* data;
} slide_memory_t;

typedef struct {
	slide_memory_t slide_memory;
} viewer_t;

typedef struct wsi_t wsi_t;
typedef struct load_tile_task_t load_tile_task_t;
struct load_tile_task_t {
	wsi_t* wsi;
	i32 level;
	i32 tile_x;
	i32 tile_y;
	u32* cached_pixels;
};

typedef struct {
	u32 texture;
	load_tile_task_t* load_task_data;
	bool32 is_submitted_for_loading;
} wsi_tile_t;

typedef struct {
	i64 width;
	i64 height;
	i64 width_in_tiles;
	i64 height_in_tiles;
	i32 num_tiles;
	wsi_tile_t* tiles;
	float um_per_pixel_x;
	float um_per_pixel_y;
	float x_tile_side_in_um;
	float y_tile_side_in_um;
} wsi_level_t;

#define WSI_MAX_LEVELS 16

typedef struct wsi_t {
	i64 width;
	i64 height;
	i64 width_pow2;
	i64 height_pow2;
	i32 num_levels;
	openslide_t* osr;
	const char* barcode;
	float mpp_x;
	float mpp_y;

	wsi_level_t levels[WSI_MAX_LEVELS];
} wsi_t;

typedef enum {
	IMAGE_TYPE_STBI_COMPATIBLE,
	IMAGE_TYPE_TIFF_GENERIC,
	IMAGE_TYPE_WSI,
} image_type_enum;

typedef struct {
	image_type_enum type;
	union {
		struct {
			i32 channels_in_file;
			i32 channels;
			i32 width;
			i32 height;
			u8* pixels;
			bool32 texture_initialized;
			u32 texture;
		} stbi;
		struct {
			i32 stub;
		} tiff;
		struct {
			wsi_t wsi;
		} wsi;
	};
} image_t;

#if 0
typedef struct {
	bool32 active;
	i32 type;
	v2i pos;
	image_t* image;
} entity_t;
#endif

// globals

extern viewer_t global_viewer;
extern bool32 use_image_adjustments;

// viewer.c

void gl_diagnostic(const char* prefix);
void first(i32 client_width, i32 client_height);
void viewer_update_and_render(input_t* input, i32 client_width, i32 client_height, float delta_t);
void on_file_dragged(char* filename);
void load_wsi(wsi_t* wsi, const char* filename);

// virtual keycodes
#define KEYCODE_LBUTTON 0x01
#define KEYCODE_RBUTTON 0x02
#define KEYCODE_CANCEL 0x03
#define KEYCODE_MBUTTON 0x04
#define KEYCODE_XBUTTON1 0x05
#define KEYCODE_XBUTTON2 0x06
#define KEYCODE_BACK 0x08
#define KEYCODE_TAB 0x09
#define KEYCODE_CLEAR 0x0C
#define KEYCODE_RETURN 0x0D
#define KEYCODE_SHIFT 0x10
#define KEYCODE_CONTROL 0x11
#define KEYCODE_MENU 0x12
#define KEYCODE_PAUSE 0x13
#define KEYCODE_CAPITAL 0x14
#define KEYCODE_KANA 0x15
#define KEYCODE_HANGEUL 0x15
#define KEYCODE_HANGUL 0x15
#define KEYCODE_JUNJA 0x17
#define KEYCODE_FINAL 0x18
#define KEYCODE_HANJA 0x19
#define KEYCODE_KANJI 0x19
#define KEYCODE_ESCAPE 0x1B
#define KEYCODE_CONVERT 0x1C
#define KEYCODE_NONCONVERT 0x1D
#define KEYCODE_ACCEPT 0x1E
#define KEYCODE_MODECHANGE 0x1F
#define KEYCODE_SPACE 0x20
#define KEYCODE_PRIOR 0x21
#define KEYCODE_NEXT 0x22
#define KEYCODE_END 0x23
#define KEYCODE_HOME 0x24
#define KEYCODE_LEFT 0x25
#define KEYCODE_UP 0x26
#define KEYCODE_RIGHT 0x27
#define KEYCODE_DOWN 0x28
#define KEYCODE_SELECT 0x29
#define KEYCODE_PRINT 0x2A
#define KEYCODE_EXECUTE 0x2B
#define KEYCODE_SNAPSHOT 0x2C
#define KEYCODE_INSERT 0x2D
#define KEYCODE_DELETE 0x2E
#define KEYCODE_HELP 0x2F
#define KEYCODE_LWIN 0x5B
#define KEYCODE_RWIN 0x5C
#define KEYCODE_APPS 0x5D
#define KEYCODE_SLEEP 0x5F
#define KEYCODE_NUMPAD0 0x60
#define KEYCODE_NUMPAD1 0x61
#define KEYCODE_NUMPAD2 0x62
#define KEYCODE_NUMPAD3 0x63
#define KEYCODE_NUMPAD4 0x64
#define KEYCODE_NUMPAD5 0x65
#define KEYCODE_NUMPAD6 0x66
#define KEYCODE_NUMPAD7 0x67
#define KEYCODE_NUMPAD8 0x68
#define KEYCODE_NUMPAD9 0x69
#define KEYCODE_MULTIPLY 0x6A
#define KEYCODE_ADD 0x6B
#define KEYCODE_SEPARATOR 0x6C
#define KEYCODE_SUBTRACT 0x6D
#define KEYCODE_DECIMAL 0x6E
#define KEYCODE_DIVIDE 0x6F
#define KEYCODE_F1 0x70
#define KEYCODE_F2 0x71
#define KEYCODE_F3 0x72
#define KEYCODE_F4 0x73
#define KEYCODE_F5 0x74
#define KEYCODE_F6 0x75
#define KEYCODE_F7 0x76
#define KEYCODE_F8 0x77
#define KEYCODE_F9 0x78
#define KEYCODE_F10 0x79
#define KEYCODE_F11 0x7A
#define KEYCODE_F12 0x7B
#define KEYCODE_F13 0x7C
#define KEYCODE_F14 0x7D
#define KEYCODE_F15 0x7E
#define KEYCODE_F16 0x7F
#define KEYCODE_F17 0x80
#define KEYCODE_F18 0x81
#define KEYCODE_F19 0x82
#define KEYCODE_F20 0x83
#define KEYCODE_F21 0x84
#define KEYCODE_F22 0x85
#define KEYCODE_F23 0x86
#define KEYCODE_F24 0x87
#define KEYCODE_NUMLOCK 0x90
#define KEYCODE_SCROLL 0x91
#define KEYCODE_OEM_NEC_EQUAL 0x92
#define KEYCODE_OEM_FJ_JISHO 0x92
#define KEYCODE_OEM_FJ_MASSHOU 0x93
#define KEYCODE_OEM_FJ_TOUROKU 0x94
#define KEYCODE_OEM_FJ_LOYA 0x95
#define KEYCODE_OEM_FJ_ROYA 0x96
#define KEYCODE_LSHIFT 0xA0
#define KEYCODE_RSHIFT 0xA1
#define KEYCODE_LCONTROL 0xA2
#define KEYCODE_RCONTROL 0xA3
#define KEYCODE_LMENU 0xA4
#define KEYCODE_RMENU 0xA5
#define KEYCODE_BROWSER_BACK 0xA6
#define KEYCODE_BROWSER_FORWARD 0xA7
#define KEYCODE_BROWSER_REFRESH 0xA8
#define KEYCODE_BROWSER_STOP 0xA9
#define KEYCODE_BROWSER_SEARCH 0xAA
#define KEYCODE_BROWSER_FAVORITES 0xAB
#define KEYCODE_BROWSER_HOME 0xAC
#define KEYCODE_VOLUME_MUTE 0xAD
#define KEYCODE_VOLUME_DOWN 0xAE
#define KEYCODE_VOLUME_UP 0xAF
#define KEYCODE_MEDIA_NEXT_TRACK 0xB0
#define KEYCODE_MEDIA_PREV_TRACK 0xB1
#define KEYCODE_MEDIA_STOP 0xB2
#define KEYCODE_MEDIA_PLAY_PAUSE 0xB3
#define KEYCODE_LAUNCH_MAIL 0xB4
#define KEYCODE_LAUNCH_MEDIA_SELECT 0xB5
#define KEYCODE_LAUNCH_APP1 0xB6
#define KEYCODE_LAUNCH_APP2 0xB7
#define KEYCODE_OEM_1 0xBA
#define KEYCODE_OEM_PLUS 0xBB
#define KEYCODE_OEM_COMMA 0xBC
#define KEYCODE_OEM_MINUS 0xBD
#define KEYCODE_OEM_PERIOD 0xBE
#define KEYCODE_OEM_2 0xBF
#define KEYCODE_OEM_3 0xC0
#define KEYCODE_OEM_4 0xDB
#define KEYCODE_OEM_5 0xDC
#define KEYCODE_OEM_6 0xDD
#define KEYCODE_OEM_7 0xDE
#define KEYCODE_OEM_8 0xDF
#define KEYCODE_OEM_AX 0xE1
#define KEYCODE_OEM_102 0xE2
#define KEYCODE_ICO_HELP 0xE3
#define KEYCODE_ICO_00 0xE4
#define KEYCODE_PROCESSKEY 0xE5
#define KEYCODE_ICO_CLEAR 0xE6
#define KEYCODE_PACKET 0xE7
#define KEYCODE_OEM_RESET 0xE9
#define KEYCODE_OEM_JUMP 0xEA
#define KEYCODE_OEM_PA1 0xEB
#define KEYCODE_OEM_PA2 0xEC
#define KEYCODE_OEM_PA3 0xED
#define KEYCODE_OEM_WSCTRL 0xEE
#define KEYCODE_OEM_CUSEL 0xEF
#define KEYCODE_OEM_ATTN 0xF0
#define KEYCODE_OEM_FINISH 0xF1
#define KEYCODE_OEM_COPY 0xF2
#define KEYCODE_OEM_AUTO 0xF3
#define KEYCODE_OEM_ENLW 0xF4
#define KEYCODE_OEM_BACKTAB 0xF5
#define KEYCODE_ATTN 0xF6
#define KEYCODE_CRSEL 0xF7
#define KEYCODE_EXSEL 0xF8
#define KEYCODE_EREOF 0xF9
#define KEYCODE_PLAY 0xFA
#define KEYCODE_ZOOM 0xFB
#define KEYCODE_NONAME 0xFC
#define KEYCODE_PA1 0xFD
#define KEYCODE_OEM_CLEAR 0xFE
