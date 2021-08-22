
#pragma once

#include <stdint.h>
typedef int8_t int8;
typedef uint8_t uint8;
typedef int16_t int16;
typedef uint16_t uint16;
typedef int32_t int32;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;

typedef struct PseudoTIFF {
	uint8* tif_data;
	uint8* tif_rawdata;
	size_t tif_rawdatasize;
	uint8* tif_rawcp;
	size_t tif_rawcc;
	void* tif_clientdata;
} PseudoTIFF;

#ifdef __cplusplus
extern "C" {
#endif

int LZWSetupDecode(PseudoTIFF* tif);
int LZWPreDecode(PseudoTIFF* tif, uint16 s);
int LZWDecode(PseudoTIFF* tif, uint8* op0, size_t occ0, uint16 s);
int LZWDecodeCompat(PseudoTIFF* tif, uint8* op0, size_t occ0, uint16 s);

#ifdef __cplusplus
}
#endif
