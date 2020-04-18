#include "common.h"

#ifdef TARGET_EMSCRIPTEN
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

EMSCRIPTEN_KEEPALIVE bool8 decode_tile(uint8_t *table_ptr, uint32_t table_length, uint8_t *input_ptr, uint32_t input_length, uint8_t *output_ptr, bool32 is_YCbCr);
EMSCRIPTEN_KEEPALIVE uint8_t *create_buffer(int size);
EMSCRIPTEN_KEEPALIVE void destroy_buffer(uint8_t *p);
