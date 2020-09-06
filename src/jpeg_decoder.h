#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

#ifdef TARGET_EMSCRIPTEN
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

void encode_tile(u8* pixels, i32 width, i32 height, i32 quality, u8** tables_buffer, u32* tables_size_ptr, u8** jpeg_buffer, u32* jpeg_size_ptr);
EMSCRIPTEN_KEEPALIVE bool8 decode_tile(uint8_t *table_ptr, uint32_t table_length, uint8_t *input_ptr, uint32_t input_length, uint8_t *output_ptr, bool32 is_YCbCr);
EMSCRIPTEN_KEEPALIVE uint8_t *create_buffer(int size);
EMSCRIPTEN_KEEPALIVE void destroy_buffer(uint8_t *p);

#ifdef __cplusplus
};
#endif
