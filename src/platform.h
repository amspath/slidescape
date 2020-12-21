/*
  Slideviewer, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2020  Pieter Valkema

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include "common.h"
#include "mathutils.h"

#if LINUX
#include <SDL2/SDL.h>
#endif

#if WINDOWS
#include "windows.h"
#else
#include <semaphore.h>
#include <unistd.h>
#endif

#ifdef TARGET_EMSCRIPTEN
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif


#ifdef __cplusplus
extern "C" {
#endif

#define MAX_THREAD_COUNT 128

typedef struct mem_t {
	size_t len;
	size_t capacity;
	u8 data[0];
} mem_t;

typedef struct memrw_t {
	u8* data;
	u64 used_size;
	u64 used_count;
	u64 capacity;
} memrw_t;

typedef void (work_queue_callback_t)(int logical_thread_index, void* userdata);

typedef struct work_queue_entry_t {
	void* data;
	work_queue_callback_t* callback;
	bool is_valid;
} work_queue_entry_t;

#if WINDOWS
typedef HANDLE semaphore_handle_t;
#else
typedef sem_t* semaphore_handle_t;
#endif

typedef struct work_queue_t {
	semaphore_handle_t semaphore;
	i32 volatile next_entry_to_submit;
	i32 volatile next_entry_to_execute;
	i32 volatile completion_count;
	i32 volatile completion_goal;
	work_queue_entry_t entries[256];
} work_queue_t;

typedef struct platform_thread_info_t {
	i32 logical_thread_index;
	work_queue_t* queue;
} platform_thread_info_t;

typedef struct {
#if WINDOWS
	HANDLE async_io_event;
	OVERLAPPED overlapped;
#else
	// TODO: implement this
#endif
	u64 thread_memory_raw_size;
	u64 thread_memory_usable_size; // free space from aligned_rest_of_thread_memory onward
	void* aligned_rest_of_thread_memory;
	u32 pbo;
} thread_memory_t;

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
	button_state_t keys[512];
	button_state_t key_shift;
	button_state_t key_ctrl;
	button_state_t key_alt;
	button_state_t key_super;
} controller_input_t;

typedef struct input_t {
	button_state_t mouse_buttons[5];
	i32 mouse_z;
	v2f drag_start_xy;
	v2f drag_vector;
	v2f mouse_xy;
	float delta_t;
	union {
		controller_input_t abstract_controllers[5];
		struct {
			controller_input_t keyboard;
			controller_input_t controllers[4];
		};
	};
	bool are_any_buttons_down;

} input_t;

#if WINDOWS
typedef HWND window_handle_t;
#elif APPLE
typedef void* window_handle_t;
#else
typedef SDL_Window* window_handle_t;
#endif

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

// Adapted from SDL_scancode.h
typedef enum
{
    SCANCODE_UNKNOWN = 0,

    /**
     *  \name Usage page 0x07
     *
     *  These values are from usage page 0x07 (USB keyboard page).
     */
    /* @{ */

    SCANCODE_A = 4,
    SCANCODE_B = 5,
    SCANCODE_C = 6,
    SCANCODE_D = 7,
    SCANCODE_E = 8,
    SCANCODE_F = 9,
    SCANCODE_G = 10,
    SCANCODE_H = 11,
    SCANCODE_I = 12,
    SCANCODE_J = 13,
    SCANCODE_K = 14,
    SCANCODE_L = 15,
    SCANCODE_M = 16,
    SCANCODE_N = 17,
    SCANCODE_O = 18,
    SCANCODE_P = 19,
    SCANCODE_Q = 20,
    SCANCODE_R = 21,
    SCANCODE_S = 22,
    SCANCODE_T = 23,
    SCANCODE_U = 24,
    SCANCODE_V = 25,
    SCANCODE_W = 26,
    SCANCODE_X = 27,
    SCANCODE_Y = 28,
    SCANCODE_Z = 29,

    SCANCODE_1 = 30,
    SCANCODE_2 = 31,
    SCANCODE_3 = 32,
    SCANCODE_4 = 33,
    SCANCODE_5 = 34,
    SCANCODE_6 = 35,
    SCANCODE_7 = 36,
    SCANCODE_8 = 37,
    SCANCODE_9 = 38,
    SCANCODE_0 = 39,

    SCANCODE_RETURN = 40,
    SCANCODE_ESCAPE = 41,
    SCANCODE_BACKSPACE = 42,
    SCANCODE_TAB = 43,
    SCANCODE_SPACE = 44,

    SCANCODE_MINUS = 45,
    SCANCODE_EQUALS = 46,
    SCANCODE_LEFTBRACKET = 47,
    SCANCODE_RIGHTBRACKET = 48,
    SCANCODE_BACKSLASH = 49, /**< Located at the lower left of the return
                                  *   key on ISO keyboards and at the right end
                                  *   of the QWERTY row on ANSI keyboards.
                                  *   Produces REVERSE SOLIDUS (backslash) and
                                  *   VERTICAL LINE in a US layout, REVERSE
                                  *   SOLIDUS and VERTICAL LINE in a UK Mac
                                  *   layout, NUMBER SIGN and TILDE in a UK
                                  *   Windows layout, DOLLAR SIGN and POUND SIGN
                                  *   in a Swiss German layout, NUMBER SIGN and
                                  *   APOSTROPHE in a German layout, GRAVE
                                  *   ACCENT and POUND SIGN in a French Mac
                                  *   layout, and ASTERISK and MICRO SIGN in a
                                  *   French Windows layout.
                                  */
    SCANCODE_NONUSHASH = 50, /**< ISO USB keyboards actually use this code
                                  *   instead of 49 for the same key, but all
                                  *   OSes I've seen treat the two codes
                                  *   identically. So, as an implementor, unless
                                  *   your keyboard generates both of those
                                  *   codes and your OS treats them differently,
                                  *   you should generate SCANCODE_BACKSLASH
                                  *   instead of this code. As a user, you
                                  *   should not rely on this code because SDL
                                  *   will never generate it with most (all?)
                                  *   keyboards.
                                  */
    SCANCODE_SEMICOLON = 51,
    SCANCODE_APOSTROPHE = 52,
    SCANCODE_GRAVE = 53, /**< Located in the top left corner (on both ANSI
                              *   and ISO keyboards). Produces GRAVE ACCENT and
                              *   TILDE in a US Windows layout and in US and UK
                              *   Mac layouts on ANSI keyboards, GRAVE ACCENT
                              *   and NOT SIGN in a UK Windows layout, SECTION
                              *   SIGN and PLUS-MINUS SIGN in US and UK Mac
                              *   layouts on ISO keyboards, SECTION SIGN and
                              *   DEGREE SIGN in a Swiss German layout (Mac:
                              *   only on ISO keyboards), CIRCUMFLEX ACCENT and
                              *   DEGREE SIGN in a German layout (Mac: only on
                              *   ISO keyboards), SUPERSCRIPT TWO and TILDE in a
                              *   French Windows layout, COMMERCIAL AT and
                              *   NUMBER SIGN in a French Mac layout on ISO
                              *   keyboards, and LESS-THAN SIGN and GREATER-THAN
                              *   SIGN in a Swiss German, German, or French Mac
                              *   layout on ANSI keyboards.
                              */
    SCANCODE_COMMA = 54,
    SCANCODE_PERIOD = 55,
    SCANCODE_SLASH = 56,

    SCANCODE_CAPSLOCK = 57,

    SCANCODE_F1 = 58,
    SCANCODE_F2 = 59,
    SCANCODE_F3 = 60,
    SCANCODE_F4 = 61,
    SCANCODE_F5 = 62,
    SCANCODE_F6 = 63,
    SCANCODE_F7 = 64,
    SCANCODE_F8 = 65,
    SCANCODE_F9 = 66,
    SCANCODE_F10 = 67,
    SCANCODE_F11 = 68,
    SCANCODE_F12 = 69,

    SCANCODE_PRINTSCREEN = 70,
    SCANCODE_SCROLLLOCK = 71,
    SCANCODE_PAUSE = 72,
    SCANCODE_INSERT = 73, /**< insert on PC, help on some Mac keyboards (but
                                   does send code 73, not 117) */
    SCANCODE_HOME = 74,
    SCANCODE_PAGEUP = 75,
    SCANCODE_DELETE = 76,
    SCANCODE_END = 77,
    SCANCODE_PAGEDOWN = 78,
    SCANCODE_RIGHT = 79,
    SCANCODE_LEFT = 80,
    SCANCODE_DOWN = 81,
    SCANCODE_UP = 82,

    SCANCODE_NUMLOCKCLEAR = 83, /**< num lock on PC, clear on Mac keyboards
                                     */
    SCANCODE_KP_DIVIDE = 84,
    SCANCODE_KP_MULTIPLY = 85,
    SCANCODE_KP_MINUS = 86,
    SCANCODE_KP_PLUS = 87,
    SCANCODE_KP_ENTER = 88,
    SCANCODE_KP_1 = 89,
    SCANCODE_KP_2 = 90,
    SCANCODE_KP_3 = 91,
    SCANCODE_KP_4 = 92,
    SCANCODE_KP_5 = 93,
    SCANCODE_KP_6 = 94,
    SCANCODE_KP_7 = 95,
    SCANCODE_KP_8 = 96,
    SCANCODE_KP_9 = 97,
    SCANCODE_KP_0 = 98,
    SCANCODE_KP_PERIOD = 99,

    SCANCODE_NONUSBACKSLASH = 100, /**< This is the additional key that ISO
                                        *   keyboards have over ANSI ones,
                                        *   located between left shift and Y.
                                        *   Produces GRAVE ACCENT and TILDE in a
                                        *   US or UK Mac layout, REVERSE SOLIDUS
                                        *   (backslash) and VERTICAL LINE in a
                                        *   US or UK Windows layout, and
                                        *   LESS-THAN SIGN and GREATER-THAN SIGN
                                        *   in a Swiss German, German, or French
                                        *   layout. */
    SCANCODE_APPLICATION = 101, /**< windows contextual menu, compose */
    SCANCODE_POWER = 102, /**< The USB document says this is a status flag,
                               *   not a physical key - but some Mac keyboards
                               *   do have a power key. */
    SCANCODE_KP_EQUALS = 103,
    SCANCODE_F13 = 104,
    SCANCODE_F14 = 105,
    SCANCODE_F15 = 106,
    SCANCODE_F16 = 107,
    SCANCODE_F17 = 108,
    SCANCODE_F18 = 109,
    SCANCODE_F19 = 110,
    SCANCODE_F20 = 111,
    SCANCODE_F21 = 112,
    SCANCODE_F22 = 113,
    SCANCODE_F23 = 114,
    SCANCODE_F24 = 115,
    SCANCODE_EXECUTE = 116,
    SCANCODE_HELP = 117,
    SCANCODE_MENU = 118,
    SCANCODE_SELECT = 119,
    SCANCODE_STOP = 120,
    SCANCODE_AGAIN = 121,   /**< redo */
    SCANCODE_UNDO = 122,
    SCANCODE_CUT = 123,
    SCANCODE_COPY = 124,
    SCANCODE_PASTE = 125,
    SCANCODE_FIND = 126,
    SCANCODE_MUTE = 127,
    SCANCODE_VOLUMEUP = 128,
    SCANCODE_VOLUMEDOWN = 129,
/* not sure whether there's a reason to enable these */
/*     SCANCODE_LOCKINGCAPSLOCK = 130,  */
/*     SCANCODE_LOCKINGNUMLOCK = 131, */
/*     SCANCODE_LOCKINGSCROLLLOCK = 132, */
    SCANCODE_KP_COMMA = 133,
    SCANCODE_KP_EQUALSAS400 = 134,

    SCANCODE_INTERNATIONAL1 = 135, /**< used on Asian keyboards, see
                                            footnotes in USB doc */
    SCANCODE_INTERNATIONAL2 = 136,
    SCANCODE_INTERNATIONAL3 = 137, /**< Yen */
    SCANCODE_INTERNATIONAL4 = 138,
    SCANCODE_INTERNATIONAL5 = 139,
    SCANCODE_INTERNATIONAL6 = 140,
    SCANCODE_INTERNATIONAL7 = 141,
    SCANCODE_INTERNATIONAL8 = 142,
    SCANCODE_INTERNATIONAL9 = 143,
    SCANCODE_LANG1 = 144, /**< Hangul/English toggle */
    SCANCODE_LANG2 = 145, /**< Hanja conversion */
    SCANCODE_LANG3 = 146, /**< Katakana */
    SCANCODE_LANG4 = 147, /**< Hiragana */
    SCANCODE_LANG5 = 148, /**< Zenkaku/Hankaku */
    SCANCODE_LANG6 = 149, /**< reserved */
    SCANCODE_LANG7 = 150, /**< reserved */
    SCANCODE_LANG8 = 151, /**< reserved */
    SCANCODE_LANG9 = 152, /**< reserved */

    SCANCODE_ALTERASE = 153, /**< Erase-Eaze */
    SCANCODE_SYSREQ = 154,
    SCANCODE_CANCEL = 155,
    SCANCODE_CLEAR = 156,
    SCANCODE_PRIOR = 157,
    SCANCODE_RETURN2 = 158,
    SCANCODE_SEPARATOR = 159,
    SCANCODE_OUT = 160,
    SCANCODE_OPER = 161,
    SCANCODE_CLEARAGAIN = 162,
    SCANCODE_CRSEL = 163,
    SCANCODE_EXSEL = 164,

    SCANCODE_KP_00 = 176,
    SCANCODE_KP_000 = 177,
    SCANCODE_THOUSANDSSEPARATOR = 178,
    SCANCODE_DECIMALSEPARATOR = 179,
    SCANCODE_CURRENCYUNIT = 180,
    SCANCODE_CURRENCYSUBUNIT = 181,
    SCANCODE_KP_LEFTPAREN = 182,
    SCANCODE_KP_RIGHTPAREN = 183,
    SCANCODE_KP_LEFTBRACE = 184,
    SCANCODE_KP_RIGHTBRACE = 185,
    SCANCODE_KP_TAB = 186,
    SCANCODE_KP_BACKSPACE = 187,
    SCANCODE_KP_A = 188,
    SCANCODE_KP_B = 189,
    SCANCODE_KP_C = 190,
    SCANCODE_KP_D = 191,
    SCANCODE_KP_E = 192,
    SCANCODE_KP_F = 193,
    SCANCODE_KP_XOR = 194,
    SCANCODE_KP_POWER = 195,
    SCANCODE_KP_PERCENT = 196,
    SCANCODE_KP_LESS = 197,
    SCANCODE_KP_GREATER = 198,
    SCANCODE_KP_AMPERSAND = 199,
    SCANCODE_KP_DBLAMPERSAND = 200,
    SCANCODE_KP_VERTICALBAR = 201,
    SCANCODE_KP_DBLVERTICALBAR = 202,
    SCANCODE_KP_COLON = 203,
    SCANCODE_KP_HASH = 204,
    SCANCODE_KP_SPACE = 205,
    SCANCODE_KP_AT = 206,
    SCANCODE_KP_EXCLAM = 207,
    SCANCODE_KP_MEMSTORE = 208,
    SCANCODE_KP_MEMRECALL = 209,
    SCANCODE_KP_MEMCLEAR = 210,
    SCANCODE_KP_MEMADD = 211,
    SCANCODE_KP_MEMSUBTRACT = 212,
    SCANCODE_KP_MEMMULTIPLY = 213,
    SCANCODE_KP_MEMDIVIDE = 214,
    SCANCODE_KP_PLUSMINUS = 215,
    SCANCODE_KP_CLEAR = 216,
    SCANCODE_KP_CLEARENTRY = 217,
    SCANCODE_KP_BINARY = 218,
    SCANCODE_KP_OCTAL = 219,
    SCANCODE_KP_DECIMAL = 220,
    SCANCODE_KP_HEXADECIMAL = 221,

    SCANCODE_LCTRL = 224,
    SCANCODE_LSHIFT = 225,
    SCANCODE_LALT = 226, /**< alt, option */
    SCANCODE_LGUI = 227, /**< windows, command (apple), meta */
    SCANCODE_RCTRL = 228,
    SCANCODE_RSHIFT = 229,
    SCANCODE_RALT = 230, /**< alt gr, option */
    SCANCODE_RGUI = 231, /**< windows, command (apple), meta */

    SCANCODE_MODE = 257,    /**< I'm not sure if this is really not covered
                                 *   by any of the above, but since there's a
                                 *   special KMOD_MODE for it I'm adding it here
                                 */

    /* @} *//* Usage page 0x07 */

    /**
     *  \name Usage page 0x0C
     *
     *  These values are mapped from usage page 0x0C (USB consumer page).
     */
    /* @{ */

    SCANCODE_AUDIONEXT = 258,
    SCANCODE_AUDIOPREV = 259,
    SCANCODE_AUDIOSTOP = 260,
    SCANCODE_AUDIOPLAY = 261,
    SCANCODE_AUDIOMUTE = 262,
    SCANCODE_MEDIASELECT = 263,
    SCANCODE_WWW = 264,
    SCANCODE_MAIL = 265,
    SCANCODE_CALCULATOR = 266,
    SCANCODE_COMPUTER = 267,
    SCANCODE_AC_SEARCH = 268,
    SCANCODE_AC_HOME = 269,
    SCANCODE_AC_BACK = 270,
    SCANCODE_AC_FORWARD = 271,
    SCANCODE_AC_STOP = 272,
    SCANCODE_AC_REFRESH = 273,
    SCANCODE_AC_BOOKMARKS = 274,

    /* @} *//* Usage page 0x0C */

    /**
     *  \name Walther keys
     *
     *  These are values that Christian Walther added (for mac keyboard?).
     */
    /* @{ */

    SCANCODE_BRIGHTNESSDOWN = 275,
    SCANCODE_BRIGHTNESSUP = 276,
    SCANCODE_DISPLAYSWITCH = 277, /**< display mirroring/dual display
                                           switch, video mode switch */
    SCANCODE_KBDILLUMTOGGLE = 278,
    SCANCODE_KBDILLUMDOWN = 279,
    SCANCODE_KBDILLUMUP = 280,
    SCANCODE_EJECT = 281,
    SCANCODE_SLEEP = 282,

    SCANCODE_APP1 = 283,
    SCANCODE_APP2 = 284,

    /* @} *//* Walther keys */

    /**
     *  \name Usage page 0x0C (additional media keys)
     *
     *  These values are mapped from usage page 0x0C (USB consumer page).
     */
    /* @{ */

    SCANCODE_AUDIOREWIND = 285,
    SCANCODE_AUDIOFASTFORWARD = 286,

    /* @} *//* Usage page 0x0C (additional media keys) */

    /* Add any other keys here. */

    NUM_SCANCODES = 512 /**< not a key, just marks the number of scancodes
                                 for array bounds */
} scancode_t;



// Platform specific function prototypes

#if !IS_SERVER
i64 get_clock();
float get_seconds_elapsed(i64 start, i64 end);
void platform_sleep(u32 ms);
void platform_sleep_ns(i64 ns);
i64 profiler_end_section(i64 start, const char* name, float report_threshold_ms);
void set_swap_interval(int interval);
#endif

u8* platform_alloc(size_t size); // required to be zeroed by the platform
mem_t* platform_allocate_mem_buffer(size_t capacity);
mem_t* platform_read_entire_file(const char* filename);
u64 file_read_at_offset(void* dest, FILE* fp, u64 offset, u64 num_bytes);

void mouse_show();
void mouse_hide();

void open_file_dialog(window_handle_t window_handle);
bool save_file_dialog(window_handle_t window, char* path_buffer, i32 path_buffer_size, const char* filter_string);
void toggle_fullscreen(window_handle_t window);
bool check_fullscreen(window_handle_t window);

void message_box(const char* message);

bool add_work_queue_entry(work_queue_t* queue, work_queue_callback_t callback, void* userdata);
bool is_queue_work_in_progress(work_queue_t* queue);
work_queue_entry_t get_next_work_queue_entry(work_queue_t* queue);
void mark_queue_entry_completed(work_queue_t* queue);
bool do_worker_work(work_queue_t* queue, int logical_thread_index);
void test_multithreading_work_queue();

bool file_exists(const char* filename);

void memrw_maybe_grow(memrw_t* buffer, u64 new_size);
u64 memrw_push(memrw_t* buffer, void* data, u64 size);
void memrw_init(memrw_t* buffer, u64 capacity);
memrw_t memrw_create(u64 capacity);
void memrw_rewind(memrw_t* buffer);
void memrw_destroy(memrw_t* buffer);

void get_system_info();

#if IS_SERVER
#define console_print printf
#define console_print_error(...) fprintf(stderr, __VA_ARGS__)
#else
void console_print(const char* fmt, ...); // defined in gui.cpp
void console_print_verbose(const char* fmt, ...); // defined in gui.cpp
void console_print_error(const char* fmt, ...);
#endif


// globals
#if defined(PLATFORM_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif

extern int g_argc;
extern const char** g_argv;
extern bool is_fullscreen;
extern bool is_program_running;
extern void* thread_local_storage[MAX_THREAD_COUNT];
extern input_t inputs[2];
extern input_t *old_input;
extern input_t *curr_input;
extern u32 os_page_size;
extern u64 page_alignment_mask;
extern i32 total_thread_count;
extern i32 worker_thread_count;
extern i32 physical_cpu_count;
extern i32 logical_cpu_count;
extern bool is_vsync_enabled;
extern bool is_nvidia_gpu;
extern bool is_macos;
extern work_queue_t global_work_queue;
extern work_queue_t global_completion_queue;
extern bool is_verbose_mode INIT(= false);

#undef INIT
#undef extern

#ifdef __cplusplus
}
#endif

