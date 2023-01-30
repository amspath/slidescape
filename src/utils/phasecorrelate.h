#pragma once

#include "common.h"
#include "mathutils.h"

typedef float real_t;
//typedef _Complex double creal_t;

typedef struct buffer2d_t {
	i32 w, h;
	real_t* data;
} buffer2d_t;


v2f phase_correlate(buffer2d_t* src1, buffer2d_t* src2, buffer2d_t* window, float background, float* response);

