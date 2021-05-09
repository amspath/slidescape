/*
  Slideviewer, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2021  Pieter Valkema

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

/*
  Decoder for whole-slide image files in iSyntax format.

  This implementation is based on the documentation on the iSyntax format released by Philips:
  https://www.openpathology.philips.com/isyntax/

  See the following documents, and the accompanying source code samples:
  - "Fast Compression Method for Medical Images on the Web", by Bas Hulsken
    https://arxiv.org/abs/2005.08713
  - The description of the iSyntax image files:
    https://www.openpathology.philips.com/wp-content/uploads/isyntax/4522%20207%2043941_2020_04_24%20Pathology%20iSyntax%20image%20format.pdf

  This implementation does not require the Philips iSyntax SDK.
*/


#include "common.h"
#include "platform.h"

#include "isyntax.h"

#include "yxml.h"

#include "jpeg_decoder.h"
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

// TODO: Implement recursive inverse wavelet transform
// TODO: Improve performance and stability
// TODO: Add ICC profiles support

#define PER_LEVEL_PADDING 3

// Code from the openjp2 library:
// inverse discrete wavelet transform (5/3)

// See: https://github.com/uclouvain/openjpeg
// The OpenJPEG license information is included below:
/*
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2003-2007, Francois-Olivier Devaux
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2007, Jonathan Ballard <dzonatas@dzonux.net>
 * Copyright (c) 2007, Callum Lerwick <seg@haxxed.com>
 * Copyright (c) 2017, IntoPIX SA <support@intopix.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
// End of OpenJPEG copyright notice.

#ifdef __AVX2__
/** Number of int32 values in a AVX2 register */
#define VREG_INT_COUNT       8
#else
/** Number of int32 values in a SSE2 register */
#define VREG_INT_COUNT       4
#endif

/** Number of columns that we can process in parallel in the vertical pass */
#define PARALLEL_COLS_53     (2*VREG_INT_COUNT)

typedef struct dwt_local {
	i32* mem;
	i32 dn;   /* number of elements in high pass band */
	i32 sn;   /* number of elements in low pass band */
	i32 cas;  /* 0 = start on even coord, 1 = start on odd coord */
} opj_dwt_t;

static void  opj_idwt53_h_cas0(i32* tmp, const i32 sn, const i32 len, i32* tiledp) {
	i32 i, j;
	const i32* in_even = &tiledp[0];
	const i32* in_odd = &tiledp[sn];

	i32 d1c, d1n, s1n, s0c, s0n;

	ASSERT(len > 1);

	/* Improved version of the TWO_PASS_VERSION: */
	/* Performs lifting in one single iteration. Saves memory */
	/* accesses and explicit interleaving. */
	s1n = in_even[0];
	d1n = in_odd[0];
	s0n = s1n - ((d1n + 1) >> 1);

	for (i = 0, j = 1; i < (len - 3); i += 2, j++) {
		d1c = d1n;
		s0c = s0n;

		s1n = in_even[j];
		d1n = in_odd[j];

		s0n = s1n - ((d1c + d1n + 2) >> 2);

		tmp[i  ] = s0c;
		tmp[i + 1] = d1c + ((s0c + s0n) >> 1);
	}

	tmp[i] = s0n;

	if (len & 1) {
		tmp[len - 1] = in_even[(len - 1) / 2] - ((d1n + 1) >> 1);
		tmp[len - 2] = d1n + ((s0n + tmp[len - 1]) >> 1);
	} else {
		tmp[len - 1] = d1n + s0n;
	}
	memcpy(tiledp, tmp, (u32)len * sizeof(i32));
}

static void  opj_idwt53_h_cas1(i32* tmp, const i32 sn, const i32 len, i32* tiledp) {
	i32 i, j;
	const i32* in_even = &tiledp[sn];
	const i32* in_odd = &tiledp[0];

	i32 s1, s2, dc, dn;

	ASSERT(len > 2);

	/* Improved version of the TWO_PASS_VERSION: */
	/* Performs lifting in one single iteration. Saves memory */
	/* accesses and explicit interleaving. */

	s1 = in_even[1];
	dc = in_odd[0] - ((in_even[0] + s1 + 2) >> 2);
	tmp[0] = in_even[0] + dc;

	for (i = 1, j = 1; i < (len - 2 - !(len & 1)); i += 2, j++) {

		s2 = in_even[j + 1];

		dn = in_odd[j] - ((s1 + s2 + 2) >> 2);
		tmp[i  ] = dc;
		tmp[i + 1] = s1 + ((dn + dc) >> 1);

		dc = dn;
		s1 = s2;
	}

	tmp[i] = dc;

	if (!(len & 1)) {
		dn = in_odd[len / 2 - 1] - ((s1 + 1) >> 1);
		tmp[len - 2] = s1 + ((dn + dc) >> 1);
		tmp[len - 1] = dn;
	} else {
		tmp[len - 1] = s1 + dc;
	}
	memcpy(tiledp, tmp, (u32)len * sizeof(i32));
}

/* <summary>                            */
/* Inverse 5-3 wavelet transform in 1-D for one row. */
/* </summary>                           */
/* Performs interleave, inverse wavelet transform and copy back to buffer */
static void opj_idwt53_h(const opj_dwt_t *dwt, i32* tiledp) {
	const i32 sn = dwt->sn;
	const i32 len = sn + dwt->dn;
	if (dwt->cas == 0) { /* Left-most sample is on even coordinate */
		if (len > 1) {
			opj_idwt53_h_cas0(dwt->mem, sn, len, tiledp);
		} else {
			/* Unmodified value */
		}
	} else { /* Left-most sample is on odd coordinate */
		if (len == 1) {
			tiledp[0] /= 2;
		} else if (len == 2) {
			i32* out = dwt->mem;
			const i32* in_even = &tiledp[sn];
			const i32* in_odd = &tiledp[0];
			out[1] = in_odd[0] - ((in_even[0] + 1) >> 1);
			out[0] = in_even[0] + out[1];
			memcpy(tiledp, dwt->mem, (u32)len * sizeof(i32));
		} else if (len > 2) {
			opj_idwt53_h_cas1(dwt->mem, sn, len, tiledp);
		}
	}
}

#if (defined(__SSE2__) || defined(__AVX2__))

/* Conveniency macros to improve the readabilty of the formulas */
#if __AVX2__
#define VREG        __m256i
#define LOAD_CST(x) _mm256_set1_epi32(x)
#define LOAD(x)     _mm256_load_si256((const VREG*)(x))
#define LOADU(x)    _mm256_loadu_si256((const VREG*)(x))
#define STORE(x,y)  _mm256_store_si256((VREG*)(x),(y))
#define STOREU(x,y) _mm256_storeu_si256((VREG*)(x),(y))
#define ADD(x,y)    _mm256_add_epi32((x),(y))
#define SUB(x,y)    _mm256_sub_epi32((x),(y))
#define SAR(x,y)    _mm256_srai_epi32((x),(y))
#else
#define VREG        __m128i
#define LOAD_CST(x) _mm_set1_epi32(x)
#define LOAD(x)     _mm_load_si128((const VREG*)(x))
#define LOADU(x)    _mm_loadu_si128((const VREG*)(x))
#define STORE(x,y)  _mm_store_si128((VREG*)(x),(y))
#define STOREU(x,y) _mm_storeu_si128((VREG*)(x),(y))
#define ADD(x,y)    _mm_add_epi32((x),(y))
#define SUB(x,y)    _mm_sub_epi32((x),(y))
#define SAR(x,y)    _mm_srai_epi32((x),(y))
#endif
#define ADD3(x,y,z) ADD(ADD(x,y),z)

static void opj_idwt53_v_final_memcpy(i32* tiledp_col, const i32* tmp, i32 len, size_t stride) {
	for (i32 i = 0; i < len; ++i) {
		/* A memcpy(&tiledp_col[i * stride + 0],
					&tmp[PARALLEL_COLS_53 * i + 0],
					PARALLEL_COLS_53 * sizeof(i32))
		   would do but would be a tiny bit slower.
		   We can take here advantage of our knowledge of alignment */
		STOREU(&tiledp_col[(size_t)i * stride + 0],
		       LOAD(&tmp[PARALLEL_COLS_53 * i + 0]));
		STOREU(&tiledp_col[(size_t)i * stride + VREG_INT_COUNT],
		       LOAD(&tmp[PARALLEL_COLS_53 * i + VREG_INT_COUNT]));
	}
}

/** Vertical inverse 5x3 wavelet transform for 8 columns in SSE2, or
 * 16 in AVX2, when top-most pixel is on even coordinate */
static void opj_idwt53_v_cas0_mcols_SSE2_OR_AVX2( i32* tmp, const i32 sn, const i32 len, i32* tiledp_col, const size_t stride) {
	const i32* in_even = &tiledp_col[0];
	const i32* in_odd = &tiledp_col[(size_t)sn * stride];

	i32 i;
	size_t j;
	VREG d1c_0, d1n_0, s1n_0, s0c_0, s0n_0;
	VREG d1c_1, d1n_1, s1n_1, s0c_1, s0n_1;
	const VREG two = LOAD_CST(2);

	ASSERT(len > 1);
#if __AVX2__
	ASSERT(PARALLEL_COLS_53 == 16);
    ASSERT(VREG_INT_COUNT == 8);
#else
	ASSERT(PARALLEL_COLS_53 == 8);
	ASSERT(VREG_INT_COUNT == 4);
#endif

	/* Note: loads of input even/odd values must be done in a unaligned */
	/* fashion. But stores in tmp can be done with aligned store, since */
	/* the temporary buffer is properly aligned */
	ASSERT((size_t)tmp % (sizeof(i32) * VREG_INT_COUNT) == 0);

	s1n_0 = LOADU(in_even + 0);
	s1n_1 = LOADU(in_even + VREG_INT_COUNT);
	d1n_0 = LOADU(in_odd);
	d1n_1 = LOADU(in_odd + VREG_INT_COUNT);

	/* s0n = s1n - ((d1n + 1) >> 1); <==> */
	/* s0n = s1n - ((d1n + d1n + 2) >> 2); */
	s0n_0 = SUB(s1n_0, SAR(ADD3(d1n_0, d1n_0, two), 2));
	s0n_1 = SUB(s1n_1, SAR(ADD3(d1n_1, d1n_1, two), 2));

	for (i = 0, j = 1; i < (len - 3); i += 2, j++) {
		d1c_0 = d1n_0;
		s0c_0 = s0n_0;
		d1c_1 = d1n_1;
		s0c_1 = s0n_1;

		s1n_0 = LOADU(in_even + j * stride);
		s1n_1 = LOADU(in_even + j * stride + VREG_INT_COUNT);
		d1n_0 = LOADU(in_odd + j * stride);
		d1n_1 = LOADU(in_odd + j * stride + VREG_INT_COUNT);

		/*s0n = s1n - ((d1c + d1n + 2) >> 2);*/
		s0n_0 = SUB(s1n_0, SAR(ADD3(d1c_0, d1n_0, two), 2));
		s0n_1 = SUB(s1n_1, SAR(ADD3(d1c_1, d1n_1, two), 2));

		STORE(tmp + PARALLEL_COLS_53 * (i + 0), s0c_0);
		STORE(tmp + PARALLEL_COLS_53 * (i + 0) + VREG_INT_COUNT, s0c_1);

		/* d1c + ((s0c + s0n) >> 1) */
		STORE(tmp + PARALLEL_COLS_53 * (i + 1) + 0,
		      ADD(d1c_0, SAR(ADD(s0c_0, s0n_0), 1)));
		STORE(tmp + PARALLEL_COLS_53 * (i + 1) + VREG_INT_COUNT,
		      ADD(d1c_1, SAR(ADD(s0c_1, s0n_1), 1)));
	}

	STORE(tmp + PARALLEL_COLS_53 * (i + 0) + 0, s0n_0);
	STORE(tmp + PARALLEL_COLS_53 * (i + 0) + VREG_INT_COUNT, s0n_1);

	if (len & 1) {
		VREG tmp_len_minus_1;
		s1n_0 = LOADU(in_even + (size_t)((len - 1) / 2) * stride);
		/* tmp_len_minus_1 = s1n - ((d1n + 1) >> 1); */
		tmp_len_minus_1 = SUB(s1n_0, SAR(ADD3(d1n_0, d1n_0, two), 2));
		STORE(tmp + PARALLEL_COLS_53 * (len - 1), tmp_len_minus_1);
		/* d1n + ((s0n + tmp_len_minus_1) >> 1) */
		STORE(tmp + PARALLEL_COLS_53 * (len - 2),
		      ADD(d1n_0, SAR(ADD(s0n_0, tmp_len_minus_1), 1)));

		s1n_1 = LOADU(in_even + (size_t)((len - 1) / 2) * stride + VREG_INT_COUNT);
		/* tmp_len_minus_1 = s1n - ((d1n + 1) >> 1); */
		tmp_len_minus_1 = SUB(s1n_1, SAR(ADD3(d1n_1, d1n_1, two), 2));
		STORE(tmp + PARALLEL_COLS_53 * (len - 1) + VREG_INT_COUNT,
		      tmp_len_minus_1);
		/* d1n + ((s0n + tmp_len_minus_1) >> 1) */
		STORE(tmp + PARALLEL_COLS_53 * (len - 2) + VREG_INT_COUNT,
		      ADD(d1n_1, SAR(ADD(s0n_1, tmp_len_minus_1), 1)));

	} else {
		STORE(tmp + PARALLEL_COLS_53 * (len - 1) + 0,
		      ADD(d1n_0, s0n_0));
		STORE(tmp + PARALLEL_COLS_53 * (len - 1) + VREG_INT_COUNT,
		      ADD(d1n_1, s0n_1));
	}

	opj_idwt53_v_final_memcpy(tiledp_col, tmp, len, stride);
}


/** Vertical inverse 5x3 wavelet transform for 8 columns in SSE2, or
 * 16 in AVX2, when top-most pixel is on odd coordinate */
static void opj_idwt53_v_cas1_mcols_SSE2_OR_AVX2(i32* tmp, const i32 sn, const i32 len, i32* tiledp_col, const size_t stride) {
	i32 i;
	size_t j;

	VREG s1_0, s2_0, dc_0, dn_0;
	VREG s1_1, s2_1, dc_1, dn_1;
	const VREG two = LOAD_CST(2);

	const i32* in_even = &tiledp_col[(size_t)sn * stride];
	const i32* in_odd = &tiledp_col[0];

	ASSERT(len > 2);
#if __AVX2__
	ASSERT(PARALLEL_COLS_53 == 16);
    ASSERT(VREG_INT_COUNT == 8);
#else
	ASSERT(PARALLEL_COLS_53 == 8);
	ASSERT(VREG_INT_COUNT == 4);
#endif

	/* Note: loads of input even/odd values must be done in a unaligned */
	/* fashion. But stores in tmp can be done with aligned store, since */
	/* the temporary buffer is properly aligned */
	ASSERT((size_t)tmp % (sizeof(i32) * VREG_INT_COUNT) == 0);

	s1_0 = LOADU(in_even + stride);
	/* in_odd[0] - ((in_even[0] + s1 + 2) >> 2); */
	dc_0 = SUB(LOADU(in_odd + 0),
	           SAR(ADD3(LOADU(in_even + 0), s1_0, two), 2));
	STORE(tmp + PARALLEL_COLS_53 * 0, ADD(LOADU(in_even + 0), dc_0));

	s1_1 = LOADU(in_even + stride + VREG_INT_COUNT);
	/* in_odd[0] - ((in_even[0] + s1 + 2) >> 2); */
	dc_1 = SUB(LOADU(in_odd + VREG_INT_COUNT),
	           SAR(ADD3(LOADU(in_even + VREG_INT_COUNT), s1_1, two), 2));
	STORE(tmp + PARALLEL_COLS_53 * 0 + VREG_INT_COUNT,
	      ADD(LOADU(in_even + VREG_INT_COUNT), dc_1));

	for (i = 1, j = 1; i < (len - 2 - !(len & 1)); i += 2, j++) {

		s2_0 = LOADU(in_even + (j + 1) * stride);
		s2_1 = LOADU(in_even + (j + 1) * stride + VREG_INT_COUNT);

		/* dn = in_odd[j * stride] - ((s1 + s2 + 2) >> 2); */
		dn_0 = SUB(LOADU(in_odd + j * stride),
		           SAR(ADD3(s1_0, s2_0, two), 2));
		dn_1 = SUB(LOADU(in_odd + j * stride + VREG_INT_COUNT),
		           SAR(ADD3(s1_1, s2_1, two), 2));

		STORE(tmp + PARALLEL_COLS_53 * i, dc_0);
		STORE(tmp + PARALLEL_COLS_53 * i + VREG_INT_COUNT, dc_1);

		/* tmp[i + 1] = s1 + ((dn + dc) >> 1); */
		STORE(tmp + PARALLEL_COLS_53 * (i + 1) + 0,
		      ADD(s1_0, SAR(ADD(dn_0, dc_0), 1)));
		STORE(tmp + PARALLEL_COLS_53 * (i + 1) + VREG_INT_COUNT,
		      ADD(s1_1, SAR(ADD(dn_1, dc_1), 1)));

		dc_0 = dn_0;
		s1_0 = s2_0;
		dc_1 = dn_1;
		s1_1 = s2_1;
	}
	STORE(tmp + PARALLEL_COLS_53 * i, dc_0);
	STORE(tmp + PARALLEL_COLS_53 * i + VREG_INT_COUNT, dc_1);

	if (!(len & 1)) {
		/*dn = in_odd[(len / 2 - 1) * stride] - ((s1 + 1) >> 1); */
		dn_0 = SUB(LOADU(in_odd + (size_t)(len / 2 - 1) * stride),
		           SAR(ADD3(s1_0, s1_0, two), 2));
		dn_1 = SUB(LOADU(in_odd + (size_t)(len / 2 - 1) * stride + VREG_INT_COUNT),
		           SAR(ADD3(s1_1, s1_1, two), 2));

		/* tmp[len - 2] = s1 + ((dn + dc) >> 1); */
		STORE(tmp + PARALLEL_COLS_53 * (len - 2) + 0,
		      ADD(s1_0, SAR(ADD(dn_0, dc_0), 1)));
		STORE(tmp + PARALLEL_COLS_53 * (len - 2) + VREG_INT_COUNT,
		      ADD(s1_1, SAR(ADD(dn_1, dc_1), 1)));

		STORE(tmp + PARALLEL_COLS_53 * (len - 1) + 0, dn_0);
		STORE(tmp + PARALLEL_COLS_53 * (len - 1) + VREG_INT_COUNT, dn_1);
	} else {
		STORE(tmp + PARALLEL_COLS_53 * (len - 1) + 0, ADD(s1_0, dc_0));
		STORE(tmp + PARALLEL_COLS_53 * (len - 1) + VREG_INT_COUNT,
		      ADD(s1_1, dc_1));
	}

	opj_idwt53_v_final_memcpy(tiledp_col, tmp, len, stride);
}

#undef VREG
#undef LOAD_CST
#undef LOADU
#undef LOAD
#undef STORE
#undef STOREU
#undef ADD
#undef ADD3
#undef SUB
#undef SAR

#endif /* (defined(__SSE2__) || defined(__AVX2__)) && !defined(STANDARD_SLOW_VERSION) */

/** Vertical inverse 5x3 wavelet transform for one column, when top-most
 * pixel is on even coordinate */
static void opj_idwt3_v_cas0(i32* tmp, const i32 sn, const i32 len, i32* tiledp_col, const size_t stride) {
	i32 i, j;
	i32 d1c, d1n, s1n, s0c, s0n;

	ASSERT(len > 1);

	/* Performs lifting in one single iteration. Saves memory */
	/* accesses and explicit interleaving. */

	s1n = tiledp_col[0];
	d1n = tiledp_col[(size_t)sn * stride];
	s0n = s1n - ((d1n + 1) >> 1);

	for (i = 0, j = 0; i < (len - 3); i += 2, j++) {
		d1c = d1n;
		s0c = s0n;

		s1n = tiledp_col[(size_t)(j + 1) * stride];
		d1n = tiledp_col[(size_t)(sn + j + 1) * stride];

		s0n = s1n - ((d1c + d1n + 2) >> 2);

		tmp[i  ] = s0c;
		tmp[i + 1] = d1c + ((s0c + s0n) >> 1);
	}

	tmp[i] = s0n;

	if (len & 1) {
		tmp[len - 1] =
				tiledp_col[(size_t)((len - 1) / 2) * stride] -
				((d1n + 1) >> 1);
		tmp[len - 2] = d1n + ((s0n + tmp[len - 1]) >> 1);
	} else {
		tmp[len - 1] = d1n + s0n;
	}

	for (i = 0; i < len; ++i) {
		tiledp_col[(size_t)i * stride] = tmp[i];
	}
}

/** Vertical inverse 5x3 wavelet transform for one column, when top-most
 * pixel is on odd coordinate */
static void opj_idwt3_v_cas1(i32* tmp, const i32 sn, const i32 len, i32* tiledp_col, const size_t stride) {
	i32 i, j;
	i32 s1, s2, dc, dn;
	const i32* in_even = &tiledp_col[(size_t)sn * stride];
	const i32* in_odd = &tiledp_col[0];

	ASSERT(len > 2);

	/* Performs lifting in one single iteration. Saves memory */
	/* accesses and explicit interleaving. */

	s1 = in_even[stride];
	dc = in_odd[0] - ((in_even[0] + s1 + 2) >> 2);
	tmp[0] = in_even[0] + dc;
	for (i = 1, j = 1; i < (len - 2 - !(len & 1)); i += 2, j++) {

		s2 = in_even[(size_t)(j + 1) * stride];

		dn = in_odd[(size_t)j * stride] - ((s1 + s2 + 2) >> 2);
		tmp[i  ] = dc;
		tmp[i + 1] = s1 + ((dn + dc) >> 1);

		dc = dn;
		s1 = s2;
	}
	tmp[i] = dc;
	if (!(len & 1)) {
		dn = in_odd[(size_t)(len / 2 - 1) * stride] - ((s1 + 1) >> 1);
		tmp[len - 2] = s1 + ((dn + dc) >> 1);
		tmp[len - 1] = dn;
	} else {
		tmp[len - 1] = s1 + dc;
	}

	for (i = 0; i < len; ++i) {
		tiledp_col[(size_t)i * stride] = tmp[i];
	}
}

/* <summary>                            */
/* Inverse vertical 5-3 wavelet transform in 1-D for several columns. */
/* </summary>                           */
/* Performs interleave, inverse wavelet transform and copy back to buffer */
static void opj_idwt53_v(const opj_dwt_t *dwt, i32* tiledp_col, size_t stride, i32 nb_cols) {
	const i32 sn = dwt->sn;
	const i32 len = sn + dwt->dn;
	if (dwt->cas == 0) {
		/* If len == 1, unmodified value */

#if (defined(__SSE2__) || defined(__AVX2__))
		if (len > 1 && nb_cols == PARALLEL_COLS_53) {
			/* Same as below general case, except that thanks to SSE2/AVX2 */
			/* we can efficiently process 8/16 columns in parallel */
			opj_idwt53_v_cas0_mcols_SSE2_OR_AVX2(dwt->mem, sn, len, tiledp_col, stride);
			return;
		}
#endif
		if (len > 1) {
			i32 c;
			for (c = 0; c < nb_cols; c++, tiledp_col++) {
				opj_idwt3_v_cas0(dwt->mem, sn, len, tiledp_col, stride);
			}
			return;
		}
	} else {
		if (len == 1) {
			i32 c;
			for (c = 0; c < nb_cols; c++, tiledp_col++) {
				tiledp_col[0] /= 2;
			}
			return;
		}

		if (len == 2) {
			i32 c;
			i32* out = dwt->mem;
			for (c = 0; c < nb_cols; c++, tiledp_col++) {
				i32 i;
				const i32* in_even = &tiledp_col[(size_t)sn * stride];
				const i32* in_odd = &tiledp_col[0];

				out[1] = in_odd[0] - ((in_even[0] + 1) >> 1);
				out[0] = in_even[0] + out[1];

				for (i = 0; i < len; ++i) {
					tiledp_col[(size_t)i * stride] = out[i];
				}
			}

			return;
		}

#if (defined(__SSE2__) || defined(__AVX2__))
		if (len > 2 && nb_cols == PARALLEL_COLS_53) {
			/* Same as below general case, except that thanks to SSE2/AVX2 */
			/* we can efficiently process 8/16 columns in parallel */
			opj_idwt53_v_cas1_mcols_SSE2_OR_AVX2(dwt->mem, sn, len, tiledp_col, stride);
			return;
		}
#endif
		if (len > 2) {
			i32 c;
			for (c = 0; c < nb_cols; c++, tiledp_col++) {
				opj_idwt3_v_cas1(dwt->mem, sn, len, tiledp_col, stride);
			}
			return;
		}
	}
}
// End of openjp2 code.


// Base64 decoder by Jouni Malinen, original:
// http://web.mit.edu/freebsd/head/contrib/wpa/src/utils/base64.c
// Performance comparison of base64 encoders/decoders:
// https://github.com/gaspardpetit/base64/

/*
 * Base64 encoding/decoding (RFC1341)
 * Copyright (c) 2005-2011, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */
static const unsigned char base64_table[65] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

unsigned char * base64_decode(const unsigned char *src, size_t len,
                              size_t *out_len)
{
	unsigned char dtable[256], *out, *pos, block[4], tmp;
	size_t i, count, olen;
	int pad = 0;

	memset(dtable, 0x80, 256);
	for (i = 0; i < sizeof(base64_table) - 1; i++)
		dtable[base64_table[i]] = (unsigned char) i;
	dtable['='] = 0;

	count = 0;
	for (i = 0; i < len; i++) {
		if (dtable[src[i]] != 0x80)
			count++;
	}

	if (count == 0 || count % 4)
		return NULL;

	olen = count / 4 * 3;
	pos = out = malloc(olen);
	if (out == NULL)
		return NULL;

	count = 0;
	for (i = 0; i < len; i++) {
		tmp = dtable[src[i]];
		if (tmp == 0x80)
			continue;

		if (src[i] == '=')
			pad++;
		block[count] = tmp;
		count++;
		if (count == 4) {
			*pos++ = (block[0] << 2) | (block[1] >> 4);
			*pos++ = (block[1] << 4) | (block[2] >> 2);
			*pos++ = (block[2] << 6) | block[3];
			count = 0;
			if (pad) {
				if (pad == 1)
					pos--;
				else if (pad == 2)
					pos -= 2;
				else {
					/* Invalid padding */
					free(out);
					return NULL;
				}
				break;
			}
		}
	}

	*out_len = pos - out;
	return out;
}
// end of base64 decoder.

// similar to atoi(), but also returning the string position so we can chain calls one after another.
static const char* atoi_and_advance(const char* str, i32* dest) {
	i32 num = 0;
	bool neg = false;
	while (isspace(*str)) ++str;
	if (*str == '-') {
		neg = true;
		++str;
	}
	while (isdigit(*str)) {
		num = 10*num + (*str - '0');
		++str;
	}
	if (neg) num = -num;
	*dest = num;
	return str;
}

static void parse_three_integers(const char* str, i32* first, i32* second, i32* third) {
	str = atoi_and_advance(str, first);
	str = atoi_and_advance(str, second);
	atoi_and_advance(str, third);
}

void isyntax_decode_base64_embedded_jpeg_file(isyntax_t* isyntax) {
	// stub
}

void isyntax_parse_ufsimport_child_node(isyntax_t* isyntax, u32 group, u32 element, char* value, u64 value_len) {

	switch(group) {
		default: {
			// unknown group
			console_print_verbose("Unknown group 0x%04x\n", group);
		} break;
		case 0x0008: {
			switch(element) {
				default: {
					console_print_verbose("Unknown element (0x%04x, 0x%04x)\n", group, element);
				} break;
				case 0x002A: /*DICOM_ACQUISITION_DATETIME*/     {} break; // "20210101103030.000000"
				case 0x0070: /*DICOM_MANUFACTURER*/             {} break; // "PHILIPS"
				case 0x1090: /*DICOM_MANUFACTURERS_MODEL_NAME*/ {} break; // "UFS Scanner"
			}
		}; break;
		case 0x0018: {
			switch(element) {
				default: {
					console_print_verbose("Unknown element (0x%04x, 0x%04x)\n", group, element);
				} break;
				case 0x1000: /*DICOM_DEVICE_SERIAL_NUMBER*/     {} break; // "FMT<4-digit number>"
				case 0x1020: /*DICOM_SOFTWARE_VERSIONS*/        {} break; // "<versionnumber>" "<versionnumber>"
				case 0x1200: /*DICOM_DATE_OF_LAST_CALIBRATION*/ {} break; // "20210101"
				case 0x1201: /*DICOM_TIME_OF_LAST_CALIBRATION*/ {} break; // "100730"
			}
		} break;
		case 0x101D: {
			switch(element) {
				default: {
					console_print_verbose("Unknown element (0x%04x, 0x%04x)\n", group, element);
				} break;
				case 0x1007: /*PIIM_DP_SCANNER_RACK_NUMBER*/        {} break; // "[1..15]"
				case 0x1008: /*PIIM_DP_SCANNER_SLOT_NUMBER*/        {} break; // "[1..15]"
				case 0x1009: /*PIIM_DP_SCANNER_OPERATOR_ID*/        {} break; // "<Operator ID>"
				case 0x100A: /*PIIM_DP_SCANNER_CALIBRATION_STATUS*/ {} break; // "OK" or "NOT OK"

			}
		} break;
		case 0x301D: {
			switch(element) {
				default: {
					console_print_verbose("Unknown element (0x%04x, 0x%04x)\n", group, element);
				} break;
				case 0x1001: /*PIM_DP_UFS_INTERFACE_VERSION*/          {} break; // "5.0"
				case 0x1002: /*PIM_DP_UFS_BARCODE*/                    {} break; // "<base64-encoded barcode value>"
				case 0x1003: /*PIM_DP_SCANNED_IMAGES*/                    {

				} break;
				case 0x1010: /*PIM_DP_SCANNER_RACK_PRIORITY*/               {} break; // "<u16>"

			}
		} break;
	}
}

void isyntax_parse_scannedimage_child_node(isyntax_t* isyntax, u32 group, u32 element, char* value, u64 value_len) {

	// Parse metadata belong to one of the images in the file (either a WSI, LABELIMAGE or MACROIMAGE)

	isyntax_image_t* image = isyntax->parser.current_image;
	if (!image) {
		image = isyntax->parser.current_image = &isyntax->images[0];
	}

	switch(group) {
		default: {
			// unknown group
			console_print_verbose("Unknown group 0x%04x\n", group);
		} break;
		case 0x0008: {
			switch(element) {
				default: {
					console_print_verbose("Unknown element (0x%04x, 0x%04x)\n", group, element);
				} break;
				case 0x2111: /*DICOM_DERIVATION_DESCRIPTION*/   {         // "PHILIPS UFS V%s | Quality=%d | DWT=%d | Compressor=%d"

				} break;
			}
		}; break;
		case 0x0028: {
			switch(element) {
				default: {
					console_print_verbose("Unknown element (0x%04x, 0x%04x)\n", group, element);
				} break;
				case 0x0002: /*DICOM_SAMPLES_PER_PIXEL*/     {} break;
				case 0x0100: /*DICOM_BITS_ALLOCATED*/     {} break;
				case 0x0101: /*DICOM_BITS_STORED*/     {} break;
				case 0x0102: /*DICOM_HIGH_BIT*/     {} break;
				case 0x0103: /*DICOM_PIXEL_REPRESENTATION*/     {} break;
				case 0x2000: /*DICOM_ICCPROFILE*/     {} break;
				case 0x2110: /*DICOM_LOSSY_IMAGE_COMPRESSION*/     {} break;
				case 0x2112: /*DICOM_LOSSY_IMAGE_COMPRESSION_RATIO*/     {} break;
				case 0x2114: /*DICOM_LOSSY_IMAGE_COMPRESSION_METHOD*/     {} break; // "PHILIPS_DP_1_0"
			}
		} break;
		case 0x301D: {
			switch(element) {
				default: {
					console_print_verbose("Unknown element (0x%04x, 0x%04x)\n", group, element);
				} break;
				case 0x1004: /*PIM_DP_IMAGE_TYPE*/                     {         // "MACROIMAGE" or "LABELIMAGE" or "WSI"
					if ((strcmp(value, "MACROIMAGE") == 0)) {
						isyntax->macro_image_index = isyntax->parser.running_image_index;
						isyntax->parser.current_image_type = ISYNTAX_IMAGE_TYPE_MACROIMAGE;
						image->image_type = ISYNTAX_IMAGE_TYPE_MACROIMAGE;
					} else if ((strcmp(value, "LABELIMAGE") == 0)) {
						isyntax->label_image_index = isyntax->parser.running_image_index;
						isyntax->parser.current_image_type = ISYNTAX_IMAGE_TYPE_LABELIMAGE;
						image->image_type = ISYNTAX_IMAGE_TYPE_LABELIMAGE;
					} else if ((strcmp(value, "WSI") == 0)) {
						isyntax->wsi_image_index = isyntax->parser.running_image_index;
						isyntax->parser.current_image_type = ISYNTAX_IMAGE_TYPE_WSI;
						image->image_type = ISYNTAX_IMAGE_TYPE_WSI;
					}
				} break;
				case 0x1005: { /*PIM_DP_IMAGE_DATA*/
					size_t decoded_capacity = value_len;
					size_t decoded_len = 0;
					i32 last_char = value[value_len-1];
					if (last_char == '/') {
						value_len--; // The last character may cause the base64 decoding to fail if invalid
					}
					u8* decoded = base64_decode((u8*)value, value_len, &decoded_len);
					if (decoded) {
						i32 channels_in_file = 0;
#if 0
						// TODO: Why does this crash?
						image->pixels = jpeg_decode_image(decoded, decoded_len, &image->width, &image->height, &channels_in_file);
#else
						// stb_image.h
						image->pixels = stbi_load_from_memory(decoded, decoded_len, &image->width, &image->height, &channels_in_file, 4);
#endif
						free(decoded);
						// TODO: actually display the image
						DUMMY_STATEMENT;
					}
				} break;
				case 0x1013: /*DP_COLOR_MANAGEMENT*/                        {} break;
				case 0x1014: /*DP_IMAGE_POST_PROCESSING*/                   {} break;
				case 0x1015: /*DP_SHARPNESS_GAIN_RGB24*/                    {} break;
				case 0x1016: /*DP_CLAHE_CLIP_LIMIT_Y16*/                    {} break;
				case 0x1017: /*DP_CLAHE_NR_BINS_Y16*/                       {} break;
				case 0x1018: /*DP_CLAHE_CONTEXT_DIMENSION_Y16*/             {} break;
				case 0x1019: /*DP_WAVELET_QUANTIZER_SETTINGS_PER_COLOR*/    {} break;
				case 0x101A: /*DP_WAVELET_QUANTIZER_SETTINGS_PER_LEVEL*/    {} break;
				case 0x101B: /*DP_WAVELET_QUANTIZER*/                       {} break;
				case 0x101C: /*DP_WAVELET_DEADZONE*/                        {} break;
				case 0x2000: /*UFS_IMAGE_GENERAL_HEADERS*/                  {} break;
				case 0x2001: /*UFS_IMAGE_NUMBER_OF_BLOCKS*/                 {} break;
				case 0x2002: /*UFS_IMAGE_DIMENSIONS_OVER_BLOCK*/            {} break;
				case 0x2003: /*UFS_IMAGE_DIMENSIONS*/                       {} break;
				case 0x2004: /*UFS_IMAGE_DIMENSION_NAME*/                   {} break;
				case 0x2005: /*UFS_IMAGE_DIMENSION_TYPE*/                   {} break;
				case 0x2006: /*UFS_IMAGE_DIMENSION_UNIT*/                   {} break;
				case 0x2007: /*UFS_IMAGE_DIMENSION_SCALE_FACTOR*/           {
					float mpp = atof(value);
					if (isyntax->parser.dimension_index == 0 /*x*/) {
						isyntax->mpp_x = mpp;
					} else if (isyntax->parser.dimension_index == 1 /*y*/) {
						isyntax->mpp_y = mpp;
					}
				} break;
				case 0x2008: /*UFS_IMAGE_DIMENSION_DISCRETE_VALUES_STRING*/ {} break;
				case 0x2009: /*UFS_IMAGE_BLOCK_HEADER_TEMPLATES*/           {} break;
				case 0x200A: /*UFS_IMAGE_DIMENSION_RANGES*/                 {} break;
				case 0x200B: /*UFS_IMAGE_DIMENSION_RANGE*/                  {
					isyntax_image_dimension_range_t range = {};
					parse_three_integers(value, &range.start, &range.step, &range.end);
					i32 step_nonzero = (range.step != 0) ? range.step : 1;
					range.numsteps = ((range.end + range.step) - range.start) / step_nonzero;
					if (isyntax->parser.data_object_flags & ISYNTAX_OBJECT_UFSImageBlockHeaderTemplate) {
						isyntax_header_template_t* template = isyntax->header_templates + isyntax->parser.header_template_index;
						switch(isyntax->parser.dimension_index) {
							default: break;
							case 0: template->block_width = range.numsteps; break;
							case 1: template->block_height = range.numsteps; break;
							case 2: template->color_component = range.start; break;
							case 3: template->scale = range.start; break;
							case 4: template->waveletcoeff = (range.start == 0) ? 1 : 3; break;
						}
						DUMMY_STATEMENT;
					} else if (isyntax->parser.data_object_flags & ISYNTAX_OBJECT_UFSImageGeneralHeader) {
						switch(isyntax->parser.dimension_index) {
							default: break;
							case 0: {
								image->offset_x = range.start;
								image->width = range.numsteps;
							} break;
							case 1: {
								image->offset_y = range.start;
								image->height = range.numsteps;
							} break;
							case 2: break; // always 3 color channels ("Y" "Co" "Cg"), no need to check
							case 3: image->num_levels = range.numsteps; break;
							case 4: break; // always 4 wavelet coefficients ("LL" "LH" "HL" "HH"), no need to check
						}
						DUMMY_STATEMENT;
					}
					DUMMY_STATEMENT;
				} break;
				case 0x200C: /*UFS_IMAGE_DIMENSION_IN_BLOCK*/               {} break;
				case 0x200F: /*UFS_IMAGE_BLOCK_COMPRESSION_METHOD*/         {} break;
				case 0x2013: /*UFS_IMAGE_PIXEL_TRANSFORMATION_METHOD*/      {} break;
				case 0x2014: { /*UFS_IMAGE_BLOCK_HEADER_TABLE*/
					size_t decoded_capacity = value_len;
					size_t decoded_len = 0;
					i32 last_char = value[value_len-1];
#if 0
					FILE* test_out = fopen("test_b64.out", "wb");
					fwrite(value, value_len, 1, test_out);
					fclose(test_out);
#endif
					if (last_char == '/') {
						value_len--; // The last character may cause the base64 decoding to fail if invalid
						last_char = value[value_len-1];
					}
					while (last_char == '\n' || last_char == '\r' || last_char == ' ') {
						--value_len;
						last_char = value[value_len-1];
					}
					u8* decoded = base64_decode((u8*)value, value_len, &decoded_len);
					if (decoded) {
						image->block_header_table = decoded;
						image->block_header_size = decoded_len;

						u32 header_size = *(u32*) decoded + 0;
						u8* block_header_start = decoded + 4;
						dicom_tag_header_t sequence_element = *(dicom_tag_header_t*) (block_header_start);
						if (sequence_element.size == 40) {
							// We have a partial header structure, with 'Block Data Offset' and 'Block Size' missing (stored in Seektable)
							// Full block header size (including the sequence element) is 48 bytes
							u32 block_count = header_size / 48;
							u32 should_be_zero = header_size % 48;
							if (should_be_zero != 0) {
								// TODO: handle error condition
								DUMMY_STATEMENT;
							}

							image->codeblock_count = block_count;
							image->codeblocks = calloc(1, block_count * sizeof(isyntax_codeblock_t));
							image->header_codeblocks_are_partial = true;

							for (i32 i = 0; i < block_count; ++i) {
								isyntax_partial_block_header_t* header = ((isyntax_partial_block_header_t*)(block_header_start)) + i;
								isyntax_codeblock_t* codeblock = image->codeblocks + i;
								codeblock->x_coordinate = header->x_coordinate;
								codeblock->y_coordinate = header->y_coordinate;
								codeblock->color_component = header->color_component;
								codeblock->scale = header->scale;
								codeblock->coefficient = header->coefficient;
								codeblock->block_header_template_id = header->block_header_template_id;
								DUMMY_STATEMENT;
							}

						} else if (sequence_element.size == 72) {
							// We have the complete header structure. (Nothing stored in Seektable)
							u32 block_count = header_size / 80;
							u32 should_be_zero = header_size % 80;
							if (should_be_zero != 0) {
								// TODO: handle error condition
								DUMMY_STATEMENT;
							}

							image->codeblock_count = block_count;
							image->codeblocks = calloc(1, block_count * sizeof(isyntax_codeblock_t));
							image->header_codeblocks_are_partial = false;

							for (i32 i = 0; i < block_count; ++i) {
								isyntax_full_block_header_t* header = ((isyntax_full_block_header_t*)(block_header_start)) + i;
								isyntax_codeblock_t* codeblock = image->codeblocks + i;
								codeblock->x_coordinate = header->x_coordinate;
								codeblock->y_coordinate = header->y_coordinate;
								codeblock->color_component = header->color_component;
								codeblock->scale = header->scale;
								codeblock->coefficient = header->coefficient;
								codeblock->block_data_offset = header->block_data_offset; // extra
								codeblock->block_size = header->block_size; // extra
								codeblock->block_header_template_id = header->block_header_template_id;
								DUMMY_STATEMENT;
							}
						} else {
							// TODO: handle error condition
						}

						free(decoded);
					} else {
						//TODO: handle error condition
					}
				} break;

			}
		} break;
	}
}

bool isyntax_validate_dicom_attr(const char* expected, const char* observed) {
	bool ok = (strcmp(expected, observed) == 0);
	if (!ok) {
		console_print("iSyntax validation error: while reading DICOM metadata, expected '%s' but found '%s'\n", expected, observed);
	}
	return ok;
}

void isyntax_parser_init(isyntax_t* isyntax) {
	isyntax_parser_t* parser = &isyntax->parser;

	parser->initialized = true;

	parser->attrbuf_capacity = KILOBYTES(32);
	parser->contentbuf_capacity = MEGABYTES(8);

	parser->current_element_name = "";
	parser->attrbuf = malloc(parser->attrbuf_capacity); // TODO: free
	parser->attrbuf_end = parser->attrbuf + parser->attrbuf_capacity;
	parser->attrcur = NULL;
	parser->attrlen = 0;
	parser->contentbuf = malloc(parser->contentbuf_capacity); // TODO: free
	parser->contentcur = NULL;
	parser->contentlen = 0;

	parser->current_dicom_attribute_name[0] = '\0';
	parser->current_dicom_group_tag = 0;
	parser->current_dicom_element_tag = 0;
	parser->attribute_index = 0;
	parser->current_node_type = ISYNTAX_NODE_NONE;

	// XML parsing using the yxml library.
	// https://dev.yorhel.nl/yxml/man
	size_t yxml_stack_buffer_size = KILOBYTES(32);
	parser->x = (yxml_t*) malloc(sizeof(yxml_t) + yxml_stack_buffer_size);

	yxml_init(parser->x, parser->x + 1, yxml_stack_buffer_size);
}

const char* get_spaces(i32 length) {
	ASSERT(length >= 0);
	static const char spaces[] = "                                  ";
	i32 spaces_len = COUNT(spaces) - 1;
	i32 offset_from_end = MIN(spaces_len, length);
	i32 offset = spaces_len - offset_from_end;
	return spaces + offset;
}

void push_to_buffer_maybe_grow(u8** restrict dest, size_t* restrict dest_len, size_t* restrict dest_capacity, void* restrict src, size_t src_len) {
	ASSERT(dest && dest_len && dest_capacity && src);
	size_t old_len = *dest_len;
	size_t new_len = old_len + src_len;
	size_t capacity = *dest_capacity;
	if (new_len > capacity) {
		capacity = next_pow2(new_len);
		u8* new_ptr = (u8*)realloc(*dest, capacity);
		if (!new_ptr) panic();
		*dest = new_ptr;
		*dest_capacity = capacity;
	}
	memcpy(*dest + old_len, src, src_len);
	*dest_len = new_len;
}

bool isyntax_parse_xml_header(isyntax_t* isyntax, char* xml_header, i64 chunk_length, bool is_last_chunk) {

	yxml_t* x = NULL;
	bool success = false;

	static bool paranoid_mode = true;

	isyntax_parser_t* parser = &isyntax->parser;

	if (!parser->initialized) {
		isyntax_parser_init(isyntax);
	}
	x = parser->x;

	if (0) { failed: cleanup:
		if (x) {
			free(x);
			parser->x = NULL;
		}
		if (parser->attrbuf) {
			free(parser->attrbuf);
			parser->attrbuf = NULL;
		}
		if (parser->contentbuf) {
			free(parser->contentbuf);
			parser->contentbuf = NULL;
		}
		return success;
	}



	// parse XML byte for byte

	char* doc = xml_header;
	for (i64 remaining_length = chunk_length; remaining_length > 0; --remaining_length, ++doc) {
		int c = *doc;
		if (c == '\0') {
			ASSERT(false); // this should never trigger
			break;
		}
		yxml_ret_t r = yxml_parse(x, c);
		if (r == YXML_OK) {
			continue; // nothing worthy of note has happened -> continue
		} else if (r < 0) {
			goto failed;
		} else if (r > 0) {
			// token
			switch(r) {
				case YXML_ELEMSTART: {
					// start of an element: '<Tag ..'
					isyntax_parser_node_t* parent_node = parser->node_stack + parser->node_stack_index;
					++parser->node_stack_index;
					isyntax_parser_node_t* node = parser->node_stack + parser->node_stack_index;
					memset(node, 0, sizeof(isyntax_parser_node_t));
					// Inherit group and element of parent node
					node->group = parent_node->group;
					node->element = parent_node->element;

					parser->contentcur = parser->contentbuf;
					*parser->contentcur = '\0';
					parser->contentlen = 0;
					parser->attribute_index = 0;
					if (strcmp(x->elem, "Attribute") == 0) {
						node->node_type = ISYNTAX_NODE_LEAF;
					} else if (strcmp(x->elem, "DataObject") == 0) {
						node->node_type = ISYNTAX_NODE_BRANCH;
						// push into the data object stack, to keep track of which type of DataObject we are parsing
						// (we need this information to restore state when the XML element ends)
						++parser->data_object_stack_index;
						parser->data_object_stack[parser->data_object_stack_index] = parent_node->element;
						// set relevant flag for which data object type we are now parsing
						u32 flags = parser->data_object_flags;
						switch(parent_node->element) {
							default: break;
							case 0:                                       flags |= ISYNTAX_OBJECT_DPUfsImport; break;
							case PIM_DP_SCANNED_IMAGES:                   flags |= ISYNTAX_OBJECT_DPScannedImage; break;
							case UFS_IMAGE_GENERAL_HEADERS:               flags |= ISYNTAX_OBJECT_UFSImageGeneralHeader; break;
							case UFS_IMAGE_BLOCK_HEADER_TEMPLATES:        flags |= ISYNTAX_OBJECT_UFSImageBlockHeaderTemplate; break;
							case UFS_IMAGE_DIMENSIONS:                    flags |= ISYNTAX_OBJECT_UFSImageDimension; break;
							case UFS_IMAGE_DIMENSION_RANGES:              flags |= ISYNTAX_OBJECT_UFSImageDimensionRange; break;
							case DP_COLOR_MANAGEMENT:                     flags |= ISYNTAX_OBJECT_DPColorManagement; break;
							case DP_IMAGE_POST_PROCESSING:                flags |= ISYNTAX_OBJECT_DPImagePostProcessing; break;
							case DP_WAVELET_QUANTIZER_SETTINGS_PER_COLOR: flags |= ISYNTAX_OBJECT_DPWaveletQuantizerSeetingsPerColor; break;
							case DP_WAVELET_QUANTIZER_SETTINGS_PER_LEVEL: flags |= ISYNTAX_OBJECT_DPWaveletQuantizerSeetingsPerLevel; break;
						}
						parser->data_object_flags = flags;
					} else if (strcmp(x->elem, "Array") == 0) {
						node->node_type = ISYNTAX_NODE_ARRAY;
						console_print_verbose("%sArray\n", get_spaces(parser->node_stack_index));
					} else {
						node->node_type = ISYNTAX_NODE_NONE;
						console_print_verbose("%selement start: %s\n", get_spaces(parser->node_stack_index), x->elem);
					}
					parser->current_node_type = node->node_type;
					parser->current_node_has_children = false;
					parser->current_element_name = x->elem; // We need to remember this pointer, because it may point to something else at the YXML_ELEMEND state

				} break;
				case YXML_CONTENT: {
					// element content
					if (!parser->contentcur) break;

					// Load iSyntax block header table (and other large XML tags) greedily and bypass yxml parsing overhead
					if (parser->current_node_type == ISYNTAX_NODE_LEAF) {
						u32 group = parser->current_dicom_group_tag;
						u32 element = parser->current_dicom_element_tag;
						isyntax_parser_node_t* node = parser->node_stack + parser->node_stack_index;
						node->group = group;
						node->element = element;
						bool need_skip = (group == 0x301D && element == 0x2014) || // UFS_IMAGE_BLOCK_HEADER_TABLE
								         (group == 0x301D && element == 0x1005) || // PIM_DP_IMAGE_DATA
										 (group == 0x0028 && element == 0x2000);   // DICOM_ICCPROFILE

					    if (need_skip) {
					    	parser->node_stack[parser->node_stack_index].has_base64_content = true;
							char* content_start = doc;
							char* pos = (char*)memchr(content_start, '<', remaining_length);
							if (pos) {
								i64 size = pos - content_start;
								push_to_buffer_maybe_grow((u8**)&parser->contentbuf, &parser->contentlen, &parser->contentbuf_capacity, content_start, size);
								parser->contentcur = parser->contentbuf + parser->contentlen;
//								console_print("iSyntax: skipped tag (0x%04x, 0x%04x) content length = %d\n", group, element, size);
								doc += (size-1); // skip to the next tag
								remaining_length -= (size-1);
								break;
							} else {
//								console_print("iSyntax: skipped tag (0x%04x, 0x%04x) content length = %d\n", group, element, remaining_length);
								push_to_buffer_maybe_grow((u8**)&parser->contentbuf, &parser->contentlen, &parser->contentbuf_capacity, content_start, remaining_length);
								parser->contentcur = parser->contentbuf + parser->contentlen;
								remaining_length = 0; // skip to the next chunk
								break;
							}
						}
					}

					char* tmp = x->data;
					while (*tmp && parser->contentlen < parser->contentbuf_capacity) {
						*(parser->contentcur++) = *(tmp++);
						++parser->contentlen;
						// too long content -> resize buffer
						if (parser->contentlen == parser->contentbuf_capacity) {
							size_t new_capacity = parser->contentbuf_capacity * 2;
							char* new_ptr = (char*)realloc(parser->contentbuf, new_capacity);
							if (!new_ptr) panic();
							parser->contentbuf = new_ptr;
							parser->contentcur = parser->contentbuf + parser->contentlen;
							parser->contentbuf_capacity = new_capacity;
//							console_print("isyntax_parse_xml_header(): XML content buffer overflow (resized buffer to %u)\n", new_capacity);
						}
					}

					*parser->contentcur = '\0';
				} break;
				case YXML_ELEMEND: {
					// end of an element: '.. />' or '</Tag>'

					if (parser->current_node_type == ISYNTAX_NODE_LEAF && !parser->current_node_has_children) {
						// Leaf node WITHOUT children.
						// In this case we didn't already parse the attributes at the YXML_ATTREND stage.
						// Now at the YXML_ELEMEND stage we can parse the complete tag at once (attributes + content).
						console_print_verbose("%sDICOM: %-40s (0x%04x, 0x%04x), size:%-8u = %s\n", get_spaces(parser->node_stack_index),
						                      parser->current_dicom_attribute_name,
						                      parser->current_dicom_group_tag, parser->current_dicom_element_tag, parser->contentlen, parser->contentbuf);

#if 0
						if (parser->node_stack[parser->node_stack_index].group == 0) {
							DUMMY_STATEMENT; // probably the group is 0 because this is a top level node.
						}
#endif

						if (parser->node_stack_index == 2) {
							isyntax_parse_ufsimport_child_node(isyntax, parser->current_dicom_group_tag, parser->current_dicom_element_tag,
							                        parser->contentbuf, parser->contentlen);
						} else {
							isyntax_parse_scannedimage_child_node(isyntax, parser->current_dicom_group_tag, parser->current_dicom_element_tag,
							                                   parser->contentbuf, parser->contentlen);
						}
					} else {
						// We have reached the end of a branch or array node, or a leaf node WITH children.
						// In this case, the attributes have already been parsed at the YXML_ATTREND stage.
						// Because their content is not text but child nodes, we do not need to touch the content buffer.
						const char* elem_name = NULL;
						if (parser->current_node_type == ISYNTAX_NODE_LEAF) {
							// End of a leaf node WITH children.
							elem_name = "Attribute";
						} else if (parser->current_node_type == ISYNTAX_NODE_BRANCH) {
							elem_name = "DataObject";
							// pop data object stack
							u16 element = parser->data_object_stack[parser->data_object_stack_index];
							--parser->data_object_stack_index;
							// reset relevant data for the data object type we are now no longer parsing
							u32 flags = parser->data_object_flags;
							switch(element) {
								default: break;
								case 0:                                       flags &= ~ISYNTAX_OBJECT_DPUfsImport; break;
								case PIM_DP_SCANNED_IMAGES:                   flags &= ~ISYNTAX_OBJECT_DPScannedImage; break;
								case UFS_IMAGE_GENERAL_HEADERS: {
									flags &= ~ISYNTAX_OBJECT_UFSImageGeneralHeader;
									parser->dimension_index = 0;
								} break;
								case UFS_IMAGE_BLOCK_HEADER_TEMPLATES: {
									flags &= ~ISYNTAX_OBJECT_UFSImageBlockHeaderTemplate;
									++parser->header_template_index;
									parser->dimension_index = 0;
								} break;
								case UFS_IMAGE_DIMENSIONS:                    flags &= ~ISYNTAX_OBJECT_UFSImageDimension; break;
								case UFS_IMAGE_DIMENSION_RANGES: {
									flags &= ~ISYNTAX_OBJECT_UFSImageDimensionRange;
									++parser->dimension_index;
								} break;
								case DP_COLOR_MANAGEMENT:                     flags &= ~ISYNTAX_OBJECT_DPColorManagement; break;
								case DP_IMAGE_POST_PROCESSING:                flags &= ~ISYNTAX_OBJECT_DPImagePostProcessing; break;
								case DP_WAVELET_QUANTIZER_SETTINGS_PER_COLOR: flags &= ~ISYNTAX_OBJECT_DPWaveletQuantizerSeetingsPerColor; break;
								case DP_WAVELET_QUANTIZER_SETTINGS_PER_LEVEL: flags &= ~ISYNTAX_OBJECT_DPWaveletQuantizerSeetingsPerLevel; break;
							}
							parser->data_object_flags = flags;
						} else if (parser->current_node_type == ISYNTAX_NODE_ARRAY) {
							elem_name = "Array";
						}

						console_print_verbose("%selement end: %s\n", get_spaces(parser->node_stack_index), elem_name);
					}

					// 'Pop' context back to parent node
					if (parser->node_stack_index > 0) {
						--parser->node_stack_index;
						parser->current_node_type = parser->node_stack[parser->node_stack_index].node_type;
						parser->current_node_has_children = parser->node_stack[parser->node_stack_index].has_children;
					} else {
						//TODO: handle error condition
						console_print_error("iSyntax XML error: closing element without matching start\n");
					}

				} break;
				case YXML_ATTRSTART: {
					// attribute: 'Name=..'
//				    console_print_verbose("attr start: %s\n", x->attr);
					parser->attrcur = parser->attrbuf;
					*parser->attrcur = '\0';
					parser->attrlen = 0;
				} break;
				case YXML_ATTRVAL: {
					// attribute value
				    //console_print_verbose("   attr val: %s\n", x->attr);
					if (!parser->attrcur) break;
					char* tmp = x->data;
					while (*tmp && parser->attrbuf < parser->attrbuf_end) {
						*(parser->attrcur++) = *(tmp++);
						++parser->attrlen;
						// too long content -> resize buffer
						if (parser->attrlen == parser->attrbuf_capacity) {
							size_t new_capacity = parser->attrbuf_capacity * 2;
							char* new_ptr = (char*)realloc(parser->attrbuf, new_capacity);
							if (!new_ptr) panic();
							parser->attrbuf = new_ptr;
							parser->attrcur = parser->attrbuf + parser->attrlen;
							parser->attrbuf_capacity = new_capacity;
//							console_print("isyntax_parse_xml_header(): XML attribute buffer overflow (resized buffer to %u)\n", new_capacity);
						}
					}
					*parser->attrcur = '\0';
				} break;
				case YXML_ATTREND: {
					// end of attribute '.."'
					if (parser->attrcur) {
						ASSERT(strlen(parser->attrbuf) == parser->attrlen);

						if (parser->current_node_type == ISYNTAX_NODE_LEAF) {
							if (parser->attribute_index == 0 /* Name="..." */) {
								if (paranoid_mode) isyntax_validate_dicom_attr(x->attr, "Name");
								size_t copy_size = MIN(parser->attrlen, sizeof(parser->current_dicom_attribute_name));
								memcpy(parser->current_dicom_attribute_name, parser->attrbuf, copy_size);
								i32 one_past_last_char = MIN(parser->attrlen, sizeof(parser->current_dicom_attribute_name)-1);
								parser->current_dicom_attribute_name[one_past_last_char] = '\0';
								DUMMY_STATEMENT;
							} else if (parser->attribute_index == 1 /* Group="0x...." */) {
								if (paranoid_mode) isyntax_validate_dicom_attr(x->attr, "Group");
								parser->current_dicom_group_tag = strtoul(parser->attrbuf, NULL, 0);
								DUMMY_STATEMENT;
							} else if (parser->attribute_index == 2 /* Element="0x...." */) {
								if (paranoid_mode) isyntax_validate_dicom_attr(x->attr, "Element");
								parser->current_dicom_element_tag = strtoul(parser->attrbuf, NULL, 0);
								DUMMY_STATEMENT;
							} else if (parser->attribute_index == 3 /* PMSVR="..." */) {
								if (paranoid_mode) isyntax_validate_dicom_attr(x->attr, "PMSVR");
								if (strcmp(parser->attrbuf, "IDataObjectArray") == 0) {
									// Leaf node WITH children.
									// Don't wait until YXML_ELEMEND to parse the attributes (this is the only opportunity we have!)
									parser->current_node_has_children = true;
									parser->node_stack[parser->node_stack_index].has_children = true;
									console_print_verbose("%sDICOM: %-40s (0x%04x, 0x%04x), array\n", get_spaces(parser->node_stack_index),
							                              parser->current_dicom_attribute_name,
									                      parser->current_dicom_group_tag, parser->current_dicom_element_tag);
									if (parser->node_stack_index == 2) { // At level of UfsImport
										isyntax_parse_ufsimport_child_node(isyntax, parser->current_dicom_group_tag, parser->current_dicom_element_tag,
										                        parser->contentbuf, parser->contentlen);
									} else {
										isyntax_parse_scannedimage_child_node(isyntax, parser->current_dicom_group_tag, parser->current_dicom_element_tag,
										                                   parser->contentbuf, parser->contentlen);
									}

								}
							}
						} else if (parser->current_node_type == ISYNTAX_NODE_BRANCH) {
							// A DataObject node is supposed to have one attribute "ObjectType"
							ASSERT(parser->attribute_index == 0);
							ASSERT(strcmp(x->attr, "ObjectType") == 0);
							console_print_verbose("%sDataObject %s = %s\n", get_spaces(parser->node_stack_index), x->attr, parser->attrbuf);
							if (strcmp(parser->attrbuf, "DPScannedImage") == 0) {
								// We started parsing a new image (which will be either a WSI, LABELIMAGE or MACROIMAGE).
								parser->current_image = isyntax->images + isyntax->image_count;
								++isyntax->image_count;
							}
						} else {
							console_print_verbose("%sattr %s = %s\n", get_spaces(parser->node_stack_index), x->attr, parser->attrbuf);
						}
						++parser->attribute_index;

					}
				} break;
				case YXML_PISTART:
				case YXML_PICONTENT:
				case YXML_PIEND:
					break; // processing instructions (uninteresting, skip)
				default: {
					console_print_error("yxml_parse(): unrecognized token (%d)\n", r);
					goto failed;
				}
			}
		}
	}

	success = true;
	if (is_last_chunk) {
		goto cleanup;
	} else {
		return success; // no cleanup yet, we will still need resources until the last header chunk is reached.
	}
}


// Convert between signed magnitude and two's complement
// https://stackoverflow.com/questions/21837008/how-to-convert-from-sign-magnitude-to-twos-complement
static inline i16 signed_magnitude_to_twos_complement_16(u16 x) {
	u16 m = -(x >> 15);
	i16 result = (~m & x) | (((x & (u16)0x8000) - x) & m);
	return result;
}

static inline i32 twos_complement_to_signed_magnitude(u32 x) {
	u32 m = -(x >> 31);
	i32 result = (~m & x) | (((x & 0x80000000) - x) & m);
	return result;
}


void debug_convert_wavelet_coefficients_to_image(isyntax_codeblock_t* codeblock) {
	if (codeblock->decoded) {
		i32 coeff_count = codeblock->coefficient ? 3 : 1;
		for (i32 i = 0; i < coeff_count; ++i) {
			u8* decoded_8bit = (u8*)malloc(128*128);
			u16* decoded = codeblock->decoded + (i * (128*128));
			for (i32 j = 0; j < 128*128; ++j) {
				u16 magnitude = decoded[j] & 0x7fff;
				decoded_8bit[j] = ATMOST(255, magnitude);
			}

			char filename[512];
			snprintf(filename, sizeof(filename), "debug_codeblock_%d.png", i);
			stbi_write_png(filename, 128, 128, 1, decoded_8bit, 128);
			free(decoded_8bit);
		}

	}
}

/*void debug_convert_wavelet_coefficients_to_image3(i32* color0, i32* color1, i32* color2, i32 width, i32 height, const char* filename) {
	if (color0) {
		u8* decoded_8bit = (u8*)malloc(width*height);
		for (i32 i = 0; i < width * height; ++i) {
			u16 magnitude = (u16)twos_complement_to_signed_magnitude(color0[i]);
			decoded_8bit[i] = ATMOST(255, magnitude);
		}

		stbi_write_png(filename, width, height, 1, decoded_8bit, width);
		free(decoded_8bit);
	}
}*/

void debug_convert_wavelet_coefficients_to_image2(i32* coefficients, i32 width, i32 height, const char* filename) {
	if (coefficients) {
		u8* decoded_8bit = (u8*)malloc(width*height);
		for (i32 i = 0; i < width * height; ++i) {
			u16 magnitude = (u16)twos_complement_to_signed_magnitude(coefficients[i]);
			decoded_8bit[i] = ATMOST(255, magnitude);
		}

		stbi_write_png(filename, width, height, 1, decoded_8bit, width);
		free(decoded_8bit);
	}
}

static u32 wavelet_coefficient_to_color_value(i32 coefficient) {
	u32 magnitude = ((u32)twos_complement_to_signed_magnitude(coefficient) & ~0x80000000);
	return magnitude;
}

static rgba_t ycocg_to_rgb(i32 Y, i32 Co, i32 Cg) {
	i32 tmp = Y - Cg/2;
	i32 G = tmp + Cg;
	i32 B = tmp - Co/2;
	i32 R = B + Co;
	return (rgba_t){ATMOST(255, R), ATMOST(255, G), ATMOST(255, B), 255};
}

static rgba_t ycocg_to_bgr(i32 Y, i32 Co, i32 Cg) {
	i32 tmp = Y - Cg/2;
	i32 G = tmp + Cg;
	i32 B = tmp - Co/2;
	i32 R = B + Co;
	return (rgba_t){ATMOST(255, B), ATMOST(255, G), ATMOST(255, R), 255};
}

void isyntax_wavelet_coefficients_to_rgb_tile(rgba_t* dest, i32* Y_coefficients, i32* Co_coefficients, i32* Cg_coefficients, i32 pixel_count) {
	for (i32 i = 0; i < pixel_count; ++i) {
		i32 Y = wavelet_coefficient_to_color_value(Y_coefficients[i]);
		dest[i] = ycocg_to_rgb(Y, Co_coefficients[i], Cg_coefficients[i]);
	}
}

void isyntax_wavelet_coefficients_to_bgr_tile(rgba_t* dest, i32* Y_coefficients, i32* Co_coefficients, i32* Cg_coefficients, i32 pixel_count) {
	for (i32 i = 0; i < pixel_count; ++i) {
		i32 Y = wavelet_coefficient_to_color_value(Y_coefficients[i]);
		dest[i] = ycocg_to_bgr(Y, Co_coefficients[i], Cg_coefficients[i]);
	}
}

#define DEBUG_OUTPUT_IDWT_STEPS_AS_PNG 0

static i32* isyntax_idwt_second_recursion(i32* ll_block, u16** h_blocks, i32 block_width, i32 block_height, i32 which_color) {
	i32 coefficients_per_block = block_width * block_height;

	u16* hl_blocks[16];
	for (i32 i = 0; i < 16; ++i) {
		hl_blocks[i] = h_blocks[i];
	}
	u16* lh_blocks[16];
	for (i32 i = 0; i < 16; ++i) {
		lh_blocks[i] = h_blocks[i] + coefficients_per_block;
	}
	u16* hh_blocks[16];
	for (i32 i = 0; i < 16; ++i) {
		hh_blocks[i] = h_blocks[i] + 2 * coefficients_per_block;
	}

	i32 pad_r = 0;
	i32 pad_l = 0;
	i32 pad_lr = pad_r + pad_l;

	i32 quadrant_width = block_width * 4;
	i32 quadrant_height = block_height * 4;
	i32 quadrant_width_padded = quadrant_width + pad_lr;
	i32 quadrant_height_padded = quadrant_height + pad_lr;
	i32 quadrant_stride = quadrant_width_padded;
	i32 full_width_padded = quadrant_width_padded * 2;
	i32 full_height_padded = quadrant_height_padded * 2;

	i32* idwt = (i32*)calloc(1, full_width_padded * full_height_padded * sizeof(i32));
	i32 idwt_stride = full_width_padded;

	// Recursive variant (non-top level):
	// LL is read from already transformed coefficients from the parent block (32-bit integers)
	// HL/LH/HH are read from codeblocks (16-bit signed magnitude)

	// Copy the LL quadrant first
	i32* ll = ll_block;
	for (i32 y = 0; y < quadrant_height; ++y) {
		i32* pos = idwt + (y + pad_r) * idwt_stride + pad_r;
		for (i32 x = 0; x < quadrant_width; ++x) {
			*pos++ = *ll++;
		}
	}
	// Fill in upper right (HL) quadrant
	i32 y_dest = pad_r;
	for (i32 block_y = 0; block_y < 4; ++block_y) {
		u16** hl_row = hl_blocks + (block_y * 4);
		for (i32 y = 0; y < block_height; ++y) {
			i32* pos = idwt + (y_dest++) * idwt_stride + quadrant_width_padded + pad_r;
			for (i32 block_x = 0; block_x < 4; ++block_x) {
				for (i32 x = 0; x < block_width; ++x) {
					*pos++ = signed_magnitude_to_twos_complement_16(*(hl_row[block_x])++);
				}
			}
		}
	}
	y_dest = quadrant_height_padded + pad_r;
	for (i32 block_y = 0; block_y < 4; ++block_y) {
		u16** lh_row = lh_blocks + (block_y * 4);
		u16** hh_row = hh_blocks + (block_y * 4);
		for (i32 y = 0; y < block_height; ++y) {
			i32* pos = idwt + (y_dest++) * idwt_stride + quadrant_width_padded + pad_r;
			for (i32 block_x = 0; block_x < 4; ++block_x) {
				for (i32 x = 0; x < block_width; ++x) {
					*pos++ = signed_magnitude_to_twos_complement_16(*(lh_row[block_x])++);
				}
			}
			pos += pad_lr;
			for (i32 block_x = 0; block_x < 4; ++block_x) {
				for (i32 x = 0; x < block_width; ++x) {
					*pos++ = signed_magnitude_to_twos_complement_16(*(hh_row[block_x])++);
				}
			}
		}
	}


#if DEBUG_OUTPUT_IDWT_STEPS_AS_PNG
	char filename[512];
	snprintf(filename, sizeof(filename), "debug_dwt_input_c%d_1.png", which_color);
	debug_convert_wavelet_coefficients_to_image2(idwt, full_width_padded, full_height_padded, filename);
#endif

	// Horizontal pass
	opj_dwt_t h = {};
	size_t dwt_mem_size = (MAX(quadrant_width_padded, quadrant_height_padded)*2) * PARALLEL_COLS_53 * sizeof(i32);
	h.mem = (i32*)_aligned_malloc(dwt_mem_size, 32);
	h.sn = quadrant_width_padded; // number of elements in low pass band
	h.dn = quadrant_width_padded; // number of elements in high pass band
	h.cas = 1;

	for (i32 y = pad_r; y < full_height_padded - pad_l; ++y) {
		i32* input_row = idwt + y * idwt_stride;
		opj_idwt53_h(&h, input_row);
	}

#if DEBUG_OUTPUT_IDWT_STEPS_AS_PNG
	snprintf(filename, sizeof(filename), "debug_dwt_input_c%d_2.png", which_color);
	debug_convert_wavelet_coefficients_to_image2(idwt, full_width_padded, full_height_padded, filename);
#endif

	// Vertical pass
	opj_dwt_t v = {};
	v.mem = h.mem;
	v.sn = quadrant_height_padded; // number of elements in low pass band
	v.dn = quadrant_height_padded; // number of elements in high pass band
	v.cas = 1;

	i32 x;
	i32 last_x = full_width_padded - pad_l;
	for (x = pad_r; x + PARALLEL_COLS_53 <= last_x; x += PARALLEL_COLS_53) {
		opj_idwt53_v(&v, idwt + x, idwt_stride, PARALLEL_COLS_53);
	}
	if (x < last_x) {
		opj_idwt53_v(&v, idwt + x, idwt_stride, (last_x - x));
	}

#if DEBUG_OUTPUT_IDWT_STEPS_AS_PNG
	snprintf(filename, sizeof(filename), "debug_dwt_input_c%d_3.png", which_color);
	debug_convert_wavelet_coefficients_to_image2(idwt, full_width_padded, full_height_padded, filename);
#endif
	_aligned_free(h.mem);
	return idwt;

}


static i32* isyntax_idwt_first_recursion(i32* ll_block, u16** h_blocks, i32 block_width, i32 block_height, i32 which_color) {
	i32 coefficients_per_block = block_width * block_height;
	u16* hl_00 = h_blocks[0];
	u16* lh_00 = h_blocks[0] + coefficients_per_block;
	u16* hh_00 = h_blocks[0] + 2 * coefficients_per_block;
	u16* hl_01 = h_blocks[1];
	u16* lh_01 = h_blocks[1] + coefficients_per_block;
	u16* hh_01 = h_blocks[1] + 2 * coefficients_per_block;
	u16* hl_10 = h_blocks[2];
	u16* lh_10 = h_blocks[2] + coefficients_per_block;
	u16* hh_10 = h_blocks[2] + 2 * coefficients_per_block;
	u16* hl_11 = h_blocks[3];
	u16* lh_11 = h_blocks[3] + coefficients_per_block;
	u16* hh_11 = h_blocks[3] + 2 * coefficients_per_block;

	i32 pad_r = 0;
	i32 pad_l = 0;
	i32 pad_lr = pad_r + pad_l;

	i32 quadrant_width = block_width * 2;
	i32 quadrant_height = block_height * 2;
	i32 quadrant_width_padded = quadrant_width + pad_lr;
	i32 quadrant_height_padded = quadrant_height + pad_lr;
	i32 quadrant_stride = quadrant_width_padded;
	i32 full_width_padded = quadrant_width_padded * 2;
	i32 full_height_padded = quadrant_height_padded * 2;

	i32* idwt = (i32*)calloc(1, full_width_padded * full_height_padded * sizeof(i32));
	i32 idwt_stride = full_width_padded;

	// Recursive variant (non-top level):
	// LL is read from already transformed coefficients from the parent block (32-bit integers)
	// HL/LH/HH are read from codeblocks (16-bit signed magnitude)

	// Copy the LL quadrant first
	i32* ll = ll_block;
	for (i32 y = 0; y < quadrant_height; ++y) {
		i32* pos = idwt + (y + pad_r) * idwt_stride + pad_r;
		for (i32 x = 0; x < quadrant_width; ++x) {
			*pos++ = *ll++;
		}
	}
	// Fill in upper right (HL) quadrant
	for (i32 y = 0; y < block_height; ++y) {
		i32* pos = idwt + (y + pad_r) * idwt_stride + quadrant_width_padded + pad_r;
		for (i32 x = 0; x < block_width; ++x) { *pos++ = signed_magnitude_to_twos_complement_16(*hl_00++); }
		for (i32 x = 0; x < block_width; ++x) { *pos++ = signed_magnitude_to_twos_complement_16(*hl_01++); }
	}
	for (i32 y = block_height; y < quadrant_height; ++y) {
		i32* pos = idwt + (y + pad_r) * idwt_stride + quadrant_width_padded + pad_r;
		for (i32 x = 0; x < block_width; ++x) { *pos++ = signed_magnitude_to_twos_complement_16(*hl_10++); }
		for (i32 x = 0; x < block_width; ++x) { *pos++ = signed_magnitude_to_twos_complement_16(*hl_11++); }
	}
	// Fill in lower quadrants (LH and HH)
	for (i32 y = quadrant_height_padded; y < quadrant_height_padded + block_height; ++y) {
		i32* pos = idwt + (y + pad_r) * idwt_stride + pad_r;
		for (i32 x = 0; x < block_width; ++x) { *pos++ = signed_magnitude_to_twos_complement_16(*lh_00++); }
		for (i32 x = 0; x < block_width; ++x) { *pos++ = signed_magnitude_to_twos_complement_16(*lh_01++); }
		pos += pad_lr;
		for (i32 x = 0; x < block_width; ++x) { *pos++ = signed_magnitude_to_twos_complement_16(*hh_00++); }
		for (i32 x = 0; x < block_width; ++x) { *pos++ = signed_magnitude_to_twos_complement_16(*hh_01++); }
	}
	for (i32 y = quadrant_height_padded + block_height; y < quadrant_height_padded + quadrant_height; ++y) {
		i32* pos = idwt + (y + pad_r) * idwt_stride + pad_r;
		for (i32 x = 0; x < block_width; ++x) { *pos++ = signed_magnitude_to_twos_complement_16(*lh_10++); }
		for (i32 x = 0; x < block_width; ++x) { *pos++ = signed_magnitude_to_twos_complement_16(*lh_11++); }
		pos += pad_lr;
		for (i32 x = 0; x < block_width; ++x) { *pos++ = signed_magnitude_to_twos_complement_16(*hh_10++); }
		for (i32 x = 0; x < block_width; ++x) { *pos++ = signed_magnitude_to_twos_complement_16(*hh_11++); }
	}

#if DEBUG_OUTPUT_IDWT_STEPS_AS_PNG
	char filename[512];
	snprintf(filename, sizeof(filename), "debug_dwt_input_c%d_1.png", which_color);
	debug_convert_wavelet_coefficients_to_image2(idwt, full_width_padded, full_height_padded, filename);
#endif

	// Horizontal pass
	opj_dwt_t h = {};
	size_t dwt_mem_size = (MAX(quadrant_width_padded, quadrant_height_padded)*2) * PARALLEL_COLS_53 * sizeof(i32);
	h.mem = (i32*)_aligned_malloc(dwt_mem_size, 32);
	h.sn = quadrant_width_padded; // number of elements in low pass band
	h.dn = quadrant_width_padded; // number of elements in high pass band
	h.cas = 1;

	for (i32 y = pad_r; y < full_height_padded - pad_l; ++y) {
		i32* input_row = idwt + y * idwt_stride;
		opj_idwt53_h(&h, input_row);
	}

#if DEBUG_OUTPUT_IDWT_STEPS_AS_PNG
	snprintf(filename, sizeof(filename), "debug_dwt_input_c%d_2.png", which_color);
	debug_convert_wavelet_coefficients_to_image2(idwt, full_width_padded, full_height_padded, filename);
#endif

	// Vertical pass
	opj_dwt_t v = {};
	v.mem = h.mem;
	v.sn = quadrant_height_padded; // number of elements in low pass band
	v.dn = quadrant_height_padded; // number of elements in high pass band
	v.cas = 1;

	i32 x;
	i32 last_x = full_width_padded - pad_l;
	for (x = pad_r; x + PARALLEL_COLS_53 <= last_x; x += PARALLEL_COLS_53) {
		opj_idwt53_v(&v, idwt + x, idwt_stride, PARALLEL_COLS_53);
	}
	if (x < last_x) {
		opj_idwt53_v(&v, idwt + x, idwt_stride, (last_x - x));
	}

#if DEBUG_OUTPUT_IDWT_STEPS_AS_PNG
	snprintf(filename, sizeof(filename), "debug_dwt_input_c%d_3.png", which_color);
	debug_convert_wavelet_coefficients_to_image2(idwt, full_width_padded, full_height_padded, filename);
#endif
	_aligned_free(h.mem);
	return idwt;

	return idwt;
}

i32* isyntax_idwt_tile(void* ll_block, u16* h_block, i32 block_width, i32 block_height, bool is_top_level, i32 which_color) {
	u16* hl = h_block;
	u16* lh = h_block + block_width * block_height;
	u16* hh = h_block + 2 * block_width * block_height;

	// Prepare the idwt buffer containing both LL and LH/Hl/HH coefficients.
	// LL | HL
	// LH | HH
	size_t idwt_buffer_size = block_width * block_height * 4 * sizeof(i32);
	i32* idwt = (i32*)_aligned_malloc(idwt_buffer_size, 32);
	i32 idwt_stride = block_width * 2;
	i32 ll_stride = (is_top_level) ? block_width : block_width * 2;

	// Stitch the top quadrants (LL and HL) together
	if (is_top_level) {
		// Top level: LL/HL/LH/HH are all read directly from a codeblock (16-bit signed magnitude)
		for (i32 y = 0; y < block_height; ++y) {
			i32* pos = idwt + y * idwt_stride;
			u16* ll = ((u16*)ll_block) + y * ll_stride;
			for (i32 x = 0; x < block_width; ++x) {
				*pos++ = signed_magnitude_to_twos_complement_16(*ll++);
			}
			for (i32 x = 0; x < block_width; ++x) {
				*pos++ = signed_magnitude_to_twos_complement_16(*hl++);
			}
		}
	} else {
		// Recursive variant (non-top level):
		// LL is read from already transformed coefficients from the parent block (32-bit integers)
		// HL/LH/HH are read from codeblocks (16-bit signed magnitude)
		for (i32 y = 0; y < block_height; ++y) {
			i32* pos = idwt + y * idwt_stride;
			i32* ll = ((i32*)ll_block) + y * ll_stride;
			for (i32 x = 0; x < block_width; ++x) {
				*pos++ = *ll++;
			}
			for (i32 x = 0; x < block_width; ++x) {
				*pos++ = signed_magnitude_to_twos_complement_16(*hl++);
			}
		}
	}
	// Now add the lower quadrants (LH and HH)
	for (i32 y = 0; y < block_height; ++y) {
		i32* pos = idwt + (y + block_height) * idwt_stride;
		for (i32 x = 0; x < block_width; ++x) {
			*pos++ = signed_magnitude_to_twos_complement_16(*lh++);
		}
		for (i32 x = 0; x < block_width; ++x) {
			*pos++ = signed_magnitude_to_twos_complement_16(*hh++);
		}
	}

#if DEBUG_OUTPUT_IDWT_STEPS_AS_PNG
	char filename[512];
	snprintf(filename, sizeof(filename), "debug_dwt_input_c%d_1.png", which_color);
	debug_convert_wavelet_coefficients_to_image2(idwt, block_width * 2, block_height * 2, filename);
#endif

	// Horizontal pass
	opj_dwt_t h = {};
	size_t dwt_mem_size = (MAX(block_width, block_height)*2) * PARALLEL_COLS_53 * sizeof(i32);
	h.mem = (i32*)_aligned_malloc(dwt_mem_size, 32);
	h.sn = block_width; // number of elements in low pass band
	h.dn = block_width; // number of elements in high pass band
	h.cas = 1;

	for (i32 y = 0; y < block_height*2; ++y) {
		i32* input_row = idwt + y * idwt_stride;
		opj_idwt53_h(&h, input_row);
	}

#if DEBUG_OUTPUT_IDWT_STEPS_AS_PNG
	snprintf(filename, sizeof(filename), "debug_dwt_input_c%d_2.png", which_color);
	debug_convert_wavelet_coefficients_to_image2(idwt, block_width * 2, block_height * 2, filename);
#endif

	// Vertical pass
	opj_dwt_t v = {};
	v.mem = h.mem;
	v.sn = block_height; // number of elements in low pass band
	v.dn = block_height; // number of elements in high pass band
	v.cas = 1;

	i32 x;
	i32 tile_width = block_width * 2;
	for (x = 0; x + PARALLEL_COLS_53 <= tile_width; x += PARALLEL_COLS_53) {
		opj_idwt53_v(&v, idwt + x, idwt_stride, PARALLEL_COLS_53);
	}
	if (x < tile_width) {
		opj_idwt53_v(&v, idwt + x, idwt_stride, (tile_width - x));
	}

#if DEBUG_OUTPUT_IDWT_STEPS_AS_PNG
	snprintf(filename, sizeof(filename), "debug_dwt_input_c%d_3.png", which_color);
	debug_convert_wavelet_coefficients_to_image2(idwt, block_width * 2, block_height * 2, filename);
#endif
	_aligned_free(h.mem);
	return idwt;
}


// Example codeblock order for a 'chunk' in the file:
// x        y       color   scale   coeff   offset      size    header_template_id
// 66302	66302	0	    8	    1	    850048253	8270	18
// 65918	65918	0	    7	    1	    850056531	17301	19
// 98686	65918	0	    7	    1	    850073840	14503	19
// 65918	98686	0	    7	    1	    850088351	8	    19
// 98686	98686	0	    7	    1	    850088367	8	    19
// 65726	65726	0	    6	    1	    850088383	26838	20
// 82110	65726	0	    6	    1	    850115229	11215	20
// 98494	65726	0	    6	    1	    850126452	6764	20
// 114878	65726	0	    6	    1	    850133224	25409	20
// 65726	82110	0	    6	    1	    850158641	21369	20
// 82110	82110	0	    6	    1	    850180018	8146	20
// 98494	82110	0	    6	    1	    850188172	4919	20
// 114878	82110	0	    6	    1	    850193099	19908	20
// 65726	98494	0	    6	    1	    850213015	8	    20
// 82110	98494	0	    6	    1	    850213031	8	    20
// 98494	98494	0	    6	    1	    850213047	8	    20
// 114878	98494	0	    6	    1	    850213063	8	    20
// 65726	114878	0	    6	    1	    850213079	8	    20
// 82110	114878	0	    6	    1	    850213095	8	    20
// 98494	114878	0	    6	    1	    850213111	8	    20
// 114878	114878	0	    6	    1	    850213127	8	    20
// 66558	66558	0	    8	    0	    850213143	5558	21    <- LL codeblock

// The above pattern repeats for the other 2 color channels (1 and 2).
// The LL codeblock is only present at the highest scales.

void isyntax_decompress_codeblock_in_chunk(isyntax_codeblock_t* codeblock, i32 block_width, i32 block_height, u8* chunk, u64 chunk_base_offset) {
	i64 offset_in_chunk = codeblock->block_data_offset - chunk_base_offset;
	ASSERT(offset_in_chunk >= 0);
	codeblock->data = chunk + offset_in_chunk;
	codeblock->decoded = isyntax_hulsken_decompress(codeblock, block_width, block_height, 1);
}

void debug_decode_wavelet_transformed_chunk(isyntax_t* isyntax, FILE* fp, isyntax_image_t* wsi, i32 base_codeblock_index, bool has_ll) {
	isyntax_codeblock_t* base_codeblock = wsi->codeblocks + base_codeblock_index;

	i32 codeblocks_per_color = isyntax_get_chunk_codeblocks_per_color_for_level(base_codeblock->scale, has_ll);
	i32 chunk_codeblock_count = codeblocks_per_color * 3;

	u64 offset0 = wsi->codeblocks[base_codeblock_index].block_data_offset;
	isyntax_codeblock_t* last_codeblock = wsi->codeblocks + base_codeblock_index + chunk_codeblock_count - 1;
	u64 offset1 = last_codeblock->block_data_offset + last_codeblock->block_size;
	u64 read_size = offset1 - offset0;

	u8* chunk = NULL;
	if (fp) {
		chunk = calloc(1, read_size + 8); // TODO: pool allocator
		fseeko64(fp, offset0, SEEK_SET);
		fread(chunk, read_size, 1, fp);

		for (i32 i = 0; i < chunk_codeblock_count; ++i) {
			isyntax_codeblock_t* codeblock = wsi->codeblocks + base_codeblock_index + i;
			i64 offset_in_chunk = codeblock->block_data_offset - offset0;
			ASSERT(offset_in_chunk >= 0);

			codeblock->data = chunk + offset_in_chunk;
			codeblock->decoded = isyntax_hulsken_decompress(codeblock, isyntax->block_width, isyntax->block_height, 1); // TODO: free using _aligned_free()
#if 0
			char out_filename[512];
			snprintf(out_filename, 512, "codeblocks/chunk_codeblock_%d_%d_%d.raw", i, codeblock->scale, codeblock->coefficient);
			FILE* out = fopen(out_filename, "wb");
			if (out) {
				fwrite(codeblock->decoded, decoded_codeblock_size_per_coefficient * (codeblock->coefficient ? 3 : 1), 1, out);
				fclose(out);
			}

			if (i == 0) debug_convert_wavelet_coefficients_to_image(codeblock);
#endif
		}

#if 0
		char out_filename[512];
		snprintf(out_filename, 512, "codeblocks/chunk.bin");
		FILE* out = fopen(out_filename, "wb");
		if (out) {
			fwrite(chunk, read_size, 1, out);
			fclose(out);
		}
#endif
		// TODO: where best to (pre)calculate this?
		u32 block_width = isyntax->block_width;
		u32 block_height = isyntax->block_height;

		if (has_ll) {
			isyntax_codeblock_t* h_blocks[3];
			isyntax_codeblock_t* ll_blocks[3];
			i32 block_color_offsets[3] = {0, codeblocks_per_color, 2 * codeblocks_per_color};
			i32 ll_block_indices[3] = {codeblocks_per_color - 1, 2 * codeblocks_per_color - 1, 3 * codeblocks_per_color - 1};
			for (i32 i = 0; i < 3; ++i) {
				h_blocks[i] = wsi->codeblocks + base_codeblock_index + block_color_offsets[i];
				ll_blocks[i] = wsi->codeblocks + base_codeblock_index + ll_block_indices[i];
			}
			for (i32 i = 0; i < 3; ++i) {
				isyntax_codeblock_t* h_block =  h_blocks[i];
				isyntax_codeblock_t* ll_block = ll_blocks[i];
				h_block->transformed = isyntax_idwt_tile(ll_block->decoded, h_block->decoded, block_width, block_height,
				                                         true, 0);
			}
			// TODO: recombine colors
			u32 tile_width = block_width * 2;
			u32 tile_height = block_height * 2;
			rgba_t* final = (rgba_t*)malloc(tile_width * tile_height * (sizeof(rgba_t)));
			i32* Y_coefficients = h_blocks[0]->transformed;
			i32* Co_coefficients = h_blocks[1]->transformed;
			i32* Cg_coefficients = h_blocks[2]->transformed;
			for (i32 i = 0; i < tile_width * tile_height; ++i) {
				i32 Y = wavelet_coefficient_to_color_value(Y_coefficients[i]);
				final[i] = ycocg_to_rgb(Y, Co_coefficients[i], Cg_coefficients[i]);
			}
#if 1
			stbi_write_png("debug_dwt_output_level2.png", tile_width, tile_height, 4, final, tile_width * 4);
#endif
			i32* color_buffers_level1[3] = {};
			i32* color_buffers_level0[3] = {};
			i32 remaining_levels_in_chunk = base_codeblock->scale % 3;
			if (remaining_levels_in_chunk-- >= 1) {
				isyntax_codeblock_t* parent_blocks[3];
				for (i32 i = 0; i < 3; ++i) {
					parent_blocks[i] = wsi->codeblocks + base_codeblock_index + block_color_offsets[i] + 1;
				}

				// stitch blocks together
				for (i32 color = 0; color < 3; ++color) {
					u16* h_decoded_blocks[4] = {};
					for (i32 i = 0; i < 4; ++i) {
						isyntax_codeblock_t* codeblock = wsi->codeblocks + base_codeblock_index + i + 1 + block_color_offsets[color];
						h_decoded_blocks[i] = codeblock->decoded;
					}
					color_buffers_level1[color] = isyntax_idwt_first_recursion(h_blocks[color]->transformed, h_decoded_blocks, block_width, block_height, color);
				}
				u32 full_width = block_width * 4;
				u32 full_height = block_height * 4;
				rgba_t* final2 = (rgba_t*)malloc(full_width * full_height * (sizeof(rgba_t)));
				Y_coefficients = color_buffers_level1[0];
				Co_coefficients = color_buffers_level1[1];
				Cg_coefficients = color_buffers_level1[2];
				for (i32 i = 0; i < full_width * full_height; ++i) {
					i32 Y = wavelet_coefficient_to_color_value(Y_coefficients[i]);
					final2[i] = ycocg_to_rgb(Y, Co_coefficients[i], Cg_coefficients[i]);
				}
#if 1
				stbi_write_png("debug_dwt_output_level1.png", full_width, full_height, 4, final2, full_width * 4);
#endif
				DUMMY_STATEMENT;

			}
			if (remaining_levels_in_chunk-- >= 1) {
				isyntax_codeblock_t* parent_blocks[3];
				for (i32 i = 0; i < 3; ++i) {
					parent_blocks[i] = wsi->codeblocks + base_codeblock_index + block_color_offsets[i] + 1;
				}

				// stitch blocks together
				for (i32 color = 0; color < 3; ++color) {
					u16* h_decoded_blocks[16] = {};
					isyntax_codeblock_t* base_codeblock_for_level = wsi->codeblocks + base_codeblock_index + block_color_offsets[color] + 5;
					for (i32 i = 0; i < 16; ++i) {
						isyntax_codeblock_t* codeblock = base_codeblock_for_level + i;
						h_decoded_blocks[i] = codeblock->decoded;
					}
					color_buffers_level0[color] = isyntax_idwt_second_recursion(color_buffers_level1[color], h_decoded_blocks, block_width, block_height, color);
				}
				u32 full_width = block_width * 8;
				u32 full_height = block_height * 8;
				rgba_t* final2 = (rgba_t*)malloc(full_width * full_height * (sizeof(rgba_t)));
				Y_coefficients = color_buffers_level0[0];
				Co_coefficients = color_buffers_level0[1];
				Cg_coefficients = color_buffers_level0[2];
				for (i32 i = 0; i < full_width * full_height; ++i) {
					i32 Y = wavelet_coefficient_to_color_value(Y_coefficients[i]);
					final2[i] = ycocg_to_rgb(Y, Co_coefficients[i], Cg_coefficients[i]);
				}
#if 1
				stbi_write_png("debug_dwt_output_level0.png", full_width, full_height, 4, final2, full_width * 4);
#endif
				DUMMY_STATEMENT;

			}


		}


	}

}


// Read between 57 and 64 bits (7 bytes + 1-8 bits) from a bitstream (least significant bit first).
// Requires that at least 7 safety bytes are present at the end of the stream (don't trigger a segmentation fault)!
static inline u64 bitstream_lsb_read(u8* buffer, u32 pos) {
	u64 raw = *(u64*)(buffer + pos / 8);
	raw >>= pos % 8;
	return raw;
}

static inline u64 bitstream_lsb_read_advance(u8* buffer, i32* bits_read, i32 bits_to_read) {
	u64 raw = *(u64*)(buffer + (*bits_read / 8));
	raw >>= (*bits_read / 8);
	*bits_read += bits_to_read;
	return raw;
}


// partly adapted from stb_image.h
#define HUFFMAN_FAST_BITS 11   // optimal value may depend on various factors, CPU cache etc.

// Lookup table for (1 << n) - 1
static const u16 size_bitmasks[17]={0,1,3,7,15,31,63,127,255,511,1023,2047,4095,8191,16383,32767,65535};

typedef struct huffman_t {
	u16 fast[1 << HUFFMAN_FAST_BITS];
	u16 code[256];
	u8  size[256];
	u16 nonfast_symbols[256];
	u16 nonfast_code[256];
	u16 nonfast_size[256];
	u16 nonfast_size_masks[256];
} huffman_t;

void save_code_in_huffman_fast_lookup_table(huffman_t* h, u32 code, u32 code_width, u8 symbol) {
	ASSERT(code_width <= HUFFMAN_FAST_BITS);
	i32 duplicate_bits = HUFFMAN_FAST_BITS - code_width;
	for (u32 i = 0; i < (1 << duplicate_bits); ++i) {
		u32 address = (i << code_width) | code;
		h->fast[address] = symbol;
	}
}

u32 max_code_size;
u32 symbol_counts[256];
u64 fast_count;
u64 nonfast_count;

u16* isyntax_hulsken_decompress(isyntax_codeblock_t* codeblock, i32 block_width, i32 block_height, i32 compressor_version) {
	ASSERT(compressor_version == 1 || compressor_version == 2);

	// Read the header information stored in the codeblock.
	// The layout varies depending on the version of the compressor used (version 1 or 2).
	// All integers are stored little-endian, least-significant bit first.
	//
	// Version 1 layout:
	//   uint32 : serialized length (in bytes)
	//   uint8 : zero run symbol
	//   uint8 : zero run counter size (in bits)
	// Version 2 layout:
	//   coeff_count (== 1 or 3) * coeff_bit_depth bits : 1 or 3 bitmasks, indicating which bitplanes are present
	//   uint8 : zero run symbol
	//   uint8 : zero run counter size (in bits)
	//   (variable length) : bitplane seektable (contains offsets to each of the bitplanes)

	// After the header section, the rest of the codeblock contains a Huffman tree, followed by a Huffman-coded
	// message of 8-bit Huffman symbols, interspersed with 'zero run' symbols (for run-length encoding of zeroes).

	i32 coeff_count = (codeblock->coefficient == 1) ? 3 : 1;
	i32 coeff_bit_depth = 16; // fixed value for iSyntax

	// Early out if dummy/empty block
	if (codeblock->block_size <= 8) {
		size_t coeff_buffer_size = coeff_count * block_width * block_height * sizeof(u16);
		u16* coeff_buffer = (u16*)calloc(1, coeff_buffer_size);
		return coeff_buffer;
	}

	i32 bits_read = 0;
	i32 block_size_in_bits = codeblock->block_size * 8;
	i64 serialized_length = 0; // In v1: stored in the first 4 bytes. In v2: derived calculation.
	u32 bitmasks[3] = { 0x000FFFF, 0x000FFFF, 0x000FFFF }; // default in v1: all ones, can be overridden later
	i32 total_mask_bits = coeff_bit_depth * coeff_count;
	u8* byte_pos = codeblock->data;
	if (compressor_version == 1) {
		serialized_length = *(u32*)byte_pos;
		byte_pos += 4;
		bits_read += 4*8;
	} else {
		if (coeff_count == 1) {
			bitmasks[0] = *(u16*)(byte_pos);
			byte_pos += 2;
			bits_read += 2*8;
			total_mask_bits = _popcnt32(bitmasks[0]);
		} else if (coeff_count == 3) {
			bitmasks[0] = *(u16*)(byte_pos);
			bitmasks[1] = *(u16*)(byte_pos+2);
			bitmasks[2] = *(u16*)(byte_pos+4);
			byte_pos += 6;
			bits_read += 6*8;
			total_mask_bits = _popcnt32(bitmasks[0]) + _popcnt32(bitmasks[1]) + _popcnt32(bitmasks[2]);
		} else {
			panic();
		}
		serialized_length = total_mask_bits * (block_width * block_height / 8);
	}
	u8 zerorun_symbol = *(u8*)byte_pos++;
	bits_read += 8;
	u8 zero_counter_size = *(u8*)byte_pos++;
	bits_read += 8;

	if (compressor_version >= 2) {
		// read bitplane seektable
		i32 stored_bit_plane_count = total_mask_bits;
		u32* bitplane_offsets = alloca(stored_bit_plane_count * sizeof(u32));
		i32 bitplane_ptr_bits = (i32)(log2f(serialized_length)) + 5;
		for (i32 i = 0; i < stored_bit_plane_count; ++i) {
			bitplane_offsets[i] = bitstream_lsb_read_advance(codeblock->data, &bits_read, bitplane_ptr_bits);
		}
	}

	// Read Huffman table
	huffman_t huffman = {};
	memset(huffman.fast, 0x80, sizeof(huffman.fast));
	memset(huffman.nonfast_size_masks, 0xFF, sizeof(huffman.nonfast_size_masks));
	u32 fast_mask = (1 << HUFFMAN_FAST_BITS) - 1;
	{
		i32 code_size = 0;
		u32 code = 0;
		i32 nonfast_symbol_index = 0;
		do {
			// Read a chunk of bits large enough to 'always' have the whole Huffman code, followed by the 8-bit symbol.
			// A blob of 57-64 bits is more than sufficient for a Huffman code of at most 16 bits.
			// The bitstream is organized least significant bit first (treat as one giant little-endian integer).
			// To read bits in the stream, look at the lowest bit positions. To advance the stream, shift right.
			i32 bits_to_advance = 1;
			u64 blob = bitstream_lsb_read(codeblock->data, bits_read); // gives back between 57 and 64 bits.

			// 'Descend' into the tree until we hit a leaf node.
			bool is_leaf = blob & 1;
			// TODO: intrinsic?
			while (!is_leaf) {
				++bits_to_advance;
				blob >>= 1;
				is_leaf = (blob & 1);
				++code_size;
			}
			blob >>= 1;

			// Read 8-bit Huffman symbol
			u8 symbol = (u8)(blob);
			huffman.code[symbol] = code;
			huffman.size[symbol] = code_size;

			if (code_size <= HUFFMAN_FAST_BITS) {
				// We can accelerate decoding of small Huffman codes by storing them in a lookup table.
				// However, for the longer codes this becomes inefficient so in those cases we need another method.
				save_code_in_huffman_fast_lookup_table(&huffman, code, code_size, symbol);
				++fast_count;
			} else {
				// TODO: how to make this faster?
				u32 prefix = code & fast_mask;
				u16 old_fast_data = huffman.fast[prefix];
				u8 old_lowest_symbol_index = old_fast_data & 0xFF;
				u8 new_lowest_symbol_index = MIN(old_lowest_symbol_index, nonfast_symbol_index);
				huffman.fast[prefix] = 256 + new_lowest_symbol_index;
				huffman.nonfast_symbols[nonfast_symbol_index] = symbol;
				huffman.nonfast_code[nonfast_symbol_index] = code;
				huffman.nonfast_size[nonfast_symbol_index] = code_size;
				huffman.nonfast_size_masks[nonfast_symbol_index] = size_bitmasks[code_size];
				++nonfast_symbol_index;
				++nonfast_count;
			}
			if (code_size > max_code_size) {
				max_code_size = code_size;
//			    console_print("found the biggest code size: %d\n", code_size);
			}
			symbol_counts[symbol]++;

			bits_to_advance += 8;
			bits_read += bits_to_advance;

			// traverse back up the tree: find last zero -> flip to one
			if (code_size == 0) {
				break; // already done; this happens if there is only a root node, no leaves
			}
			u32 code_high_bit = (1 << (code_size - 1));
			bool found_zero = (~code) & code_high_bit;
			while (!found_zero) {
				--code_size;
				if (code_size == 0) break;
				code &= code_high_bit - 1;
				code_high_bit >>= 1;
				found_zero = (~code) & code_high_bit;
			}
			code |= code_high_bit;
		} while(code_size > 0);
	}

#if 0 // for running without decoding, for debugging and performance measurements
	u8* decompressed_buffer = NULL;
	i32 decompressed_length = 0;
	return NULL;
#else
	// Decode the message
	u8* decompressed_buffer = (u8*)malloc(serialized_length);

	u32 zerorun_code = huffman.code[zerorun_symbol];
	u32 zerorun_code_size = huffman.size[zerorun_symbol];
	if (zerorun_code_size == 0) zerorun_code_size = 1; // handle special case of the 'empty' Huffman tree (root node is leaf node)
	u32 zerorun_code_mask = (1 << zerorun_code_size) - 1;

	u32 zero_counter_mask = (1 << zero_counter_size) - 1;
	i32 decompressed_length = 0;
	while (bits_read < block_size_in_bits) {
		if (decompressed_length >= serialized_length) {
			break; // done
		}
		i32 symbol = 0;
		i32 code_size = 1;
		u64 blob = bitstream_lsb_read(codeblock->data, bits_read);
		u32 fast_index = blob & fast_mask;
		u16 c = huffman.fast[fast_index];
		if (c <= 255) {
			// Lookup the symbol directly.
			symbol = c;
			code_size = huffman.size[symbol];
		} else {
			// TODO: super naive and slow implementation, accelerate this!
			bool match = false;
			u8 lowest_possible_symbol_index = c & 0xFF;

#if 1//(defined(__SSE2__) && defined(__AVX__))
			for (i32 i = lowest_possible_symbol_index; i < 256; ++i) {
				u8 test_size = huffman.nonfast_size[i];
				u16 test_code = huffman.nonfast_code[i];
				if ((blob & size_bitmasks[test_size]) == test_code) {
					// match
					code_size = test_size;
					symbol = huffman.nonfast_symbols[i];
					match = true;
					break;
				}
			}
#else
			// SIMD version using SSE2, about as fast as the version above
			// (Need to compile with AVX enabled, otherwise unaligned loads will make it slower)
			// Can it be made faster?
			i32 the_symbol = 0;
			for (i32 i = lowest_possible_symbol_index/8; i < 256; i += 8) {
				__m128i_u size_mask = _mm_loadu_si128((__m128i_u*)(huffman.nonfast_size_masks + i));
				__m128i_u code = _mm_loadu_si128((__m128i_u*)(huffman.nonfast_code + i));
				__m128i_u test = _mm_set1_epi16((u16)blob);
				test = _mm_and_si128(test, size_mask);
				__m128i hit = _mm_cmpeq_epi16(test, code);
				u32 hit_mask = _mm_movemask_epi8(hit);
				if (hit_mask) {
					unsigned long first_bit = 0;
					_BitScanForward(&first_bit, hit_mask);
					i32 symbol_index = i + first_bit / 2;
					symbol = huffman.nonfast_symbols[symbol_index];
					code_size = huffman.nonfast_size[symbol_index];
					match = true;
					break;
				}
			}
			DUMMY_STATEMENT;
			if (the_symbol != symbol) {
				DUMMY_STATEMENT;
			}
#endif
			if (!match) {
				DUMMY_STATEMENT;
			}
		}

		if (code_size == 0) code_size = 1; // handle special case of the 'empty' Huffman tree (root node is leaf node)

		blob >>= code_size;
		bits_read += code_size;

		// Handle run-length encoding of zeroes
		if (symbol == zerorun_symbol) {
			u32 numzeroes = blob & zero_counter_mask;
			bits_read += zero_counter_size;
			// A 'zero run' with length of zero means that this is not a zero run after all, but rather
			// the 'escaped' zero run symbol itself which should be outputted.
			if (numzeroes > 0) {
				if (compressor_version == 2) ++numzeroes; // v2 stores actual count minus one
				if (decompressed_length + numzeroes >= serialized_length) {
					// Reached the end, terminate
					memset(decompressed_buffer + decompressed_length, 0, MIN(serialized_length - decompressed_length, numzeroes));
					decompressed_length += numzeroes;
					break;
				}
				// If the next Huffman symbol is also the zero run symbol, then their counters actually refer to the same zero run.
				// Basically, each extra zero run symbol expands the 'zero counter' bit depth, i.e.:
				//   n zero symbols -> depth becomes n * counter_bits
				u32 total_zero_counter_size = zero_counter_size;
				for(;;) {
					// Peek ahead in the bitstream, grab any additional zero run symbols, and recalculate numzeroes.
					blob = bitstream_lsb_read(codeblock->data, bits_read);
					u8 next_code = (blob & zerorun_code_mask);
					if (next_code == zerorun_code) {
						// The zero run continues
						blob >>= zerorun_code_size;
						u32 counter_extra_bits = blob & zero_counter_mask;
						if (compressor_version == 2) ++counter_extra_bits; // v2 stores actual count minus one
						numzeroes <<= zero_counter_size;
						numzeroes |= (counter_extra_bits);
						total_zero_counter_size += zero_counter_size;
						bits_read += zerorun_code_size + zero_counter_size;
						if (decompressed_length + numzeroes >= serialized_length) {
							break; // Reached the end, terminate
						}
					} else {
						break; // no next zero run symbol, the zero run is finished
					}
				}

				i32 bytes_to_write = MIN(serialized_length - decompressed_length, numzeroes);
				ASSERT(bytes_to_write > 0);
				memset(decompressed_buffer + decompressed_length, 0, bytes_to_write);
				decompressed_length += numzeroes;
			} else {
				// This is not a 'zero run' after all, but an escaped symbol. So output the symbol.
				decompressed_buffer[decompressed_length++] = symbol;
			}
		} else {
			decompressed_buffer[decompressed_length++] = symbol;
		}

	}
#endif

	if (serialized_length != decompressed_length) {
		ASSERT(!"size mismatch");
		console_print("iSyntax: size mismatch in block %d (size=%d): expected %d observed %d\n",
				codeblock->block_data_offset, codeblock->block_size, serialized_length, decompressed_length);
	}
	codeblock->decompressed_size = decompressed_length;

	i32 bytes_per_bitplane = (block_width * block_height) / 8;
	if (compressor_version == 1) {

		i32 bytes_per_sample = 2; // ((coeff_bit_depth+7)/8);
		i32 expected_bitmask_bits = (decompressed_length*8) / (block_width * block_height);

		// try to deduce the number of coefficients without knowing the header information
		// TODO: this should not be necessary! Remove this code once we reliably know coeff_count from the header information
		i32 extra_bits = (decompressed_length*8) % (block_width * block_height);
		if (extra_bits > 0) {
			if (coeff_count != 1 && extra_bits == 1*16) {
				coeff_count = 1;
			} else if (coeff_count != 3 && extra_bits == 3*16) {
				coeff_count = 3;
			}
			total_mask_bits = coeff_bit_depth * coeff_count;
		}

		// If there are empty bitplanes: bitmasks stored at end of data
		u64 expected_length = total_mask_bits * bytes_per_bitplane;
		if (decompressed_length < expected_length) {
			if (coeff_count == 1) {
				bitmasks[0] = *(u16*)(decompressed_buffer + decompressed_length - 2);
				total_mask_bits = _popcnt32(bitmasks[0]);
			} else if (coeff_count == 3) {
				byte_pos = decompressed_buffer + decompressed_length - 6;
				bitmasks[0] = *(u16*)(byte_pos);
				bitmasks[1] = *(u16*)(byte_pos+2);
				bitmasks[2] = *(u16*)(byte_pos+4);
				total_mask_bits = _popcnt32(bitmasks[0]) + _popcnt32(bitmasks[1]) + _popcnt32(bitmasks[2]);
			} else {
				panic(); // TODO: fail condition
			}
			expected_length = (total_mask_bits * block_width * block_height) / 8 + (coeff_count * 2);
			ASSERT(decompressed_length == expected_length);
		}
	}

	// unpack bitplanes
	i32 compressed_bitplane_index = 0;
	size_t coeff_buffer_size = coeff_count * block_width * block_height * sizeof(u16);
	u16* coeff_buffer = (u16*)_aligned_malloc(coeff_buffer_size, 32);
	u16* final_coeff_buffer = (u16*)_aligned_malloc(coeff_buffer_size, 32); // this copy will have the snake-order reshuffling undone
	memset(coeff_buffer, 0, coeff_buffer_size);
	memset(final_coeff_buffer, 0, coeff_buffer_size);

	for (i32 coeff_index = 0; coeff_index < coeff_count; ++coeff_index) {
		u16 bitmask = bitmasks[coeff_index];
		u16* current_coeff_buffer = coeff_buffer + (coeff_index * (block_width * block_height));
		u16* current_final_coeff_buffer = final_coeff_buffer + (coeff_index * (block_width * block_height));
#if 1

		i32 bit = 0;
		while (bitmask) {
			if (bitmask & 1) {
				ASSERT((block_width * block_height % 8) == 0);
				u8* bitplane = decompressed_buffer + (compressed_bitplane_index * bytes_per_bitplane);
				for (i32 i = 0; i < block_width * block_height; i += 8) {
					i32 j = i/8;
					// What is the order bitplanes are stored in, actually??
					i32 shift_amount = (bit == 0) ? 15 : bit - 1; // bitplanes are stored sign, lsb ... msb
//					i32 shift_amount = 15 - bit; // bitplanes are stored sign, msb ... lsb
					u8 b = bitplane[j];
					if (b == 0) continue;
#if !defined(__SSE2__)
					// TODO: SIMD stuff; this step is SLOW
					current_coeff_buffer[i+0] |= ((b >> 0) & 1) << shift_amount;
					current_coeff_buffer[i+1] |= ((b >> 1) & 1) << shift_amount;
					current_coeff_buffer[i+2] |= ((b >> 2) & 1) << shift_amount;
					current_coeff_buffer[i+3] |= ((b >> 3) & 1) << shift_amount;
					current_coeff_buffer[i+4] |= ((b >> 4) & 1) << shift_amount;
					current_coeff_buffer[i+5] |= ((b >> 5) & 1) << shift_amount;
					current_coeff_buffer[i+6] |= ((b >> 6) & 1) << shift_amount;
					current_coeff_buffer[i+7] |= ((b >> 7) & 1) << shift_amount;
#else
					// This SIMD implementation is ~20% faster compared to the simple version above.
					// Can it be made faster?
					__m128i* dst = (__m128i*) (current_coeff_buffer+i);
					uint64_t t = _bswap64(((0x8040201008040201ULL*b) & 0x8080808080808080ULL) >> 7);
					__m128i v_t = _mm_set_epi64x(0, t);
					__m128i array_of_bools = _mm_unpacklo_epi8(v_t, _mm_setzero_si128());
					__m128i masks = _mm_slli_epi16(array_of_bools, shift_amount);
					__m128i result = _mm_or_si128(*dst, masks);
					*dst = result;
#endif
					DUMMY_STATEMENT;
				}
				++compressed_bitplane_index;
			}
			bitmask >>= 1;
			++bit;

		}
#if 1
		// Reshuffle snake-order
		if (bit > 0) {
			i32 area_stride_x = block_width / 4;
			for (i32 area4x4_index = 0; area4x4_index < ((block_width * block_height) / 16); ++area4x4_index) {
				i32 area_base_index = area4x4_index * 16;
				i32 area_x = (area4x4_index % area_stride_x) * 4;
				i32 area_y = (area4x4_index / area_stride_x) * 4;

				u64 area_y0 = *(u64*)&current_coeff_buffer[area_base_index];
				u64 area_y1 = *(u64*)&current_coeff_buffer[area_base_index+4];
				u64 area_y2 = *(u64*)&current_coeff_buffer[area_base_index+8];
				u64 area_y3 = *(u64*)&current_coeff_buffer[area_base_index+12];

				*(u64*)(current_final_coeff_buffer + (area_y+0) * block_width + area_x) = area_y0;
				*(u64*)(current_final_coeff_buffer + (area_y+1) * block_width + area_x) = area_y1;
				*(u64*)(current_final_coeff_buffer + (area_y+2) * block_width + area_x) = area_y2;
				*(u64*)(current_final_coeff_buffer + (area_y+3) * block_width + area_x) = area_y3;
			}

			/*for (i32 i = 0; i < block_width * block_height; ++i) {
				final_coeff_buffer[i] = signed_magnitude_to_twos_complement_16(final_coeff_buffer[i]);
			}*/
		}
#endif
#endif

	}

	_aligned_free(coeff_buffer);
	free(decompressed_buffer);
//	_aligned_free(final_coeff_buffer);



	return final_coeff_buffer;

}

void debug_read_codeblock_from_file(isyntax_codeblock_t* codeblock, FILE* fp) {
	if (fp && !codeblock->data) {
		codeblock->data = calloc(1, codeblock->block_size + 8); // TODO: pool allocator
		fseeko64(fp, codeblock->block_data_offset, SEEK_SET);
		fread(codeblock->data, codeblock->block_size, 1, fp);

#if 0
		char out_filename[512];
		snprintf(out_filename, 512, "codeblocks/%d.bin", codeblock->block_data_offset);
		FILE* out = fopen(out_filename, "wb");
		if (out) {
			fwrite(codeblock->data, codeblock->block_size, 1, out);
			fclose(out);
		}
#endif
	}
}

static inline i32 get_first_valid_coef_pixel(i32 scale) {
	i32 result = (PER_LEVEL_PADDING << scale) - (PER_LEVEL_PADDING - 1);
	return result;
}

static inline i32 get_first_valid_ll_pixel(i32 scale) {
	i32 result = get_first_valid_coef_pixel(scale) + (1 << scale);
	return result;
}

i32 isyntax_get_chunk_codeblocks_per_color_for_level(i32 level, bool has_ll) {
	i32 rel_level = level % 3;
	i32 codeblock_count;
	if (rel_level == 0) {
		codeblock_count = 1;
	} else if (rel_level == 1) {
		codeblock_count = 1 + 4;
	} else {
		codeblock_count = 1 + 4 + 16;
	}
	if (has_ll) ++codeblock_count;
	return codeblock_count;
}


static void test_output_block_header(isyntax_image_t* wsi_image) {
	FILE* test_block_header_fp = fopen("test_block_header.csv", "wb");
	if (test_block_header_fp) {
		fprintf(test_block_header_fp, "x_coordinate,y_coordinate,color_component,scale,coefficient,block_data_offset,block_data_size,block_header_template_id\n");

		for (i32 i = 0; i < wsi_image->codeblock_count; i += 1/*21*3*/) {
			isyntax_codeblock_t* codeblock = wsi_image->codeblocks + i;
			fprintf(test_block_header_fp, "%d,%d,%d,%d,%d,%d,%d,%d\n",
//			        codeblock->x_adjusted,
//			        codeblock->y_adjusted,
			        codeblock->x_coordinate - wsi_image->offset_x,
			        codeblock->y_coordinate - wsi_image->offset_y,
			        codeblock->color_component,
			        codeblock->scale,
			        codeblock->coefficient,
			        codeblock->block_data_offset,
			        codeblock->block_size,
			        codeblock->block_header_template_id);

//			if (codeblock->scale == wsi_image->num_levels-1) {
//				i+=3; // skip extra LL blocks;
//			}
		}

		fclose(test_block_header_fp);
	}
}

bool isyntax_open(isyntax_t* isyntax, const char* filename) {

	ASSERT(isyntax);
	memset(isyntax, 0, sizeof(*isyntax));

	int ret = 0; (void)ret;
	FILE* fp = fopen64(filename, "rb");
	bool success = false;
	if (fp) {
		struct stat st;
		if (fstat(fileno(fp), &st) == 0) {
			isyntax->filesize = st.st_size;

			// https://www.openpathology.philips.com/wp-content/uploads/isyntax/4522%20207%2043941_2020_04_24%20Pathology%20iSyntax%20image%20format.pdf
			// Layout of an iSyntax file:
			// XML Header | End of Table (EOT) marker, 3 bytes "\r\n\x04" | Seektable (optional) | Codeblocks

			// Read the XML header
			// We don't know the length of the XML header, so we just read 'enough' data in a chunk and hope that we get it
			// (and if not, read some more data until we get it)

			i64 load_begin = get_clock();
			i64 io_begin = get_clock();
			i64 io_ticks_elapsed = 0;
			i64 parse_begin = get_clock();
			i64 parse_ticks_elapsed = 0;

			size_t read_size = MEGABYTES(1);
			char* read_buffer = malloc(read_size);
			size_t bytes_read = fread(read_buffer, 1, read_size, fp);
			io_ticks_elapsed += (get_clock() - io_begin);

			if (bytes_read < 3) goto fail_1;
			bool are_there_bytes_left = (bytes_read == read_size);

			// find EOT candidates, 3 bytes "\r\n\x04"
			i64 header_length = 0;
			i64 isyntax_data_offset = 0; // Offset of either the Seektable, or the Codeblocks segment of the iSyntax file.
			i64 bytes_read_from_data_offset_in_last_chunk = 0;

			i32 chunk_index = 0;
			for (;; ++chunk_index) {
//				console_print("iSyntax: reading XML header chunk %d\n", chunk_index);
				i64 chunk_length = 0;
				bool match = false;
				char* pos = read_buffer;
				i64 offset = 0;
				char* marker = (char*)memchr(read_buffer, '\x04', bytes_read);
				if (marker) {
					offset = marker - read_buffer;
					match = true;
					chunk_length = offset;
					header_length += chunk_length;
					isyntax_data_offset = header_length + 1;
					i64 data_offset_in_last_chunk = offset + 1;
					bytes_read_from_data_offset_in_last_chunk = (i64)bytes_read - data_offset_in_last_chunk;
				}
				if (match) {
					// We found the end of the XML header. This is the last chunk to process.
					if (!(header_length > 0 && header_length < isyntax->filesize)) goto fail_1;

					parse_begin = get_clock();
					isyntax_parse_xml_header(isyntax, read_buffer, chunk_length, true);
					parse_ticks_elapsed += (get_clock() - parse_begin);

//					console_print("iSyntax: the XML header is %u bytes, or %g%% of the total file size\n", header_length, (float)((float)header_length * 100.0f) / isyntax->filesize);
//					console_print("   I/O time: %g seconds\n", get_seconds_elapsed(0, io_ticks_elapsed));
//					console_print("   Parsing time: %g seconds\n", get_seconds_elapsed(0, parse_ticks_elapsed));
//					console_print("   Total loading time: %g seconds\n", get_seconds_elapsed(load_begin, get_clock()));
					break;
				} else {
					// We didn't find the end of the XML header. We need to read more chunks to find it.
					// (Or, we reached the end of the file unexpectedly, which is an error.)
					chunk_length = read_size;
					header_length += chunk_length;
					if (are_there_bytes_left) {

						parse_begin = get_clock();
						isyntax_parse_xml_header(isyntax, read_buffer, chunk_length, false);
						parse_ticks_elapsed += (get_clock() - parse_begin);

						io_begin = get_clock();
						bytes_read = fread(read_buffer, 1, read_size, fp); // read the next chunk
						io_ticks_elapsed += (get_clock() - io_begin);

						are_there_bytes_left = (bytes_read == read_size);
						continue;
					} else {
						console_print_error("iSyntax parsing error: didn't find the end of the XML header (unexpected end of file)\n");
						goto fail_1;
					}
				}
			}

			if (isyntax->mpp_x <= 0.0f) {
				isyntax->mpp_x = 0.25f; // should usually be 0.25; zero or below can never be right
			}
			if (isyntax->mpp_y <= 0.0f) {
				isyntax->mpp_y = 0.25f;
			}

			isyntax->block_width = isyntax->header_templates[0].block_width;
			isyntax->block_height = isyntax->header_templates[0].block_height;
			isyntax->tile_width = isyntax->block_width * 2; // tile dimension AFTER inverse wavelet transform
			isyntax->tile_height = isyntax->block_height * 2;

			isyntax_image_t* wsi_image = isyntax->images + isyntax->wsi_image_index;
			if (wsi_image->image_type == ISYNTAX_IMAGE_TYPE_WSI) {

				i64 block_width = isyntax->block_width;
				i64 block_height = isyntax->block_height;
				i64 tile_width = isyntax->tile_width;
				i64 tile_height = isyntax->tile_height;

				i64 num_levels = wsi_image->num_levels;
				ASSERT(num_levels >= 1);
//				i64 padding = (PER_LEVEL_PADDING << num_levels) - PER_LEVEL_PADDING;
				i64 grid_width = ((wsi_image->width + (block_width << num_levels) - 1) / (block_width << num_levels)) << (num_levels - 1);
				i64 grid_height = ((wsi_image->height + (block_height << num_levels) - 1) / (block_height << num_levels)) << (num_levels - 1);

				i64 h_coeff_tile_count = 0; // number of tiles with LH/HL/HH coefficients
				i64 base_level_tile_count = grid_height * grid_width;
				for (i32 i = 0; i < wsi_image->num_levels; ++i) {
					isyntax_level_t* level = wsi_image->levels + i;
					level->tile_count = base_level_tile_count >> (i * 2);
					h_coeff_tile_count += level->tile_count;
					level->scale = i;
					level->width_in_tiles = grid_width >> i;
					level->height_in_tiles = grid_height >> i;
				}
				// The highest level has LL tiles in addition to LH/HL/HH tiles
				i64 ll_coeff_tile_count = base_level_tile_count >> ((num_levels - 1) * 2);
				i64 total_coeff_tile_count = h_coeff_tile_count + ll_coeff_tile_count;
				i64 total_codeblock_count = total_coeff_tile_count * 3; // for 3 color channels

				for (i32 i = 0; i < wsi_image->codeblock_count; ++i) {
					isyntax_codeblock_t* codeblock = wsi_image->codeblocks + i;

					// Calculate adjusted codeblock coordinates so that they fit the origin of the image
					codeblock->x_adjusted = (i32)codeblock->x_coordinate - wsi_image->offset_x;
					codeblock->y_adjusted = (i32)codeblock->y_coordinate - wsi_image->offset_y;

					// Calculate the block ID (= index into the seektable)
					// adapted from extract_block_header.py
					bool is_ll = codeblock->coefficient == 0;
					u32 block_id = 0;
					i32 maxscale = is_ll ? codeblock->scale + 1 : codeblock->scale;
					for (i32 scale = 0; scale < maxscale; ++scale) {
						block_id += wsi_image->levels[scale].tile_count;
					}

					i32 offset;
					if (is_ll) {
						offset = get_first_valid_ll_pixel(codeblock->scale);
					} else {
						offset = get_first_valid_coef_pixel(codeblock->scale);
					}
					i32 x = codeblock->x_adjusted - offset;
					i32 y = codeblock->y_adjusted - offset;
					codeblock->x_adjusted = x;
					codeblock->y_adjusted = y;
					codeblock->block_x = x / (tile_width << codeblock->scale);
					codeblock->block_y = y / (tile_height << codeblock->scale);

					i32 grid_stride = grid_width >> codeblock->scale;
					block_id += codeblock->block_y * grid_stride + codeblock->block_x;

					i32 tiles_per_color = total_coeff_tile_count;
					block_id += codeblock->color_component * tiles_per_color;
					codeblock->block_id = block_id;
					DUMMY_STATEMENT;
				}

				io_begin = get_clock(); // for performance measurement
				fseeko64(fp, isyntax_data_offset, SEEK_SET);
				if (wsi_image->header_codeblocks_are_partial) {
					// The seektable is required to be present, because the block header table did not contain all information.
					dicom_tag_header_t seektable_header_tag = {};
					fread(&seektable_header_tag, sizeof(dicom_tag_header_t), 1, fp);

					io_ticks_elapsed += (get_clock() - io_begin);
					parse_begin = get_clock();

					if (seektable_header_tag.group == 0x301D && seektable_header_tag.element == 0x2015) {
						i32 seektable_size = seektable_header_tag.size;
						if (seektable_size < 0) {
							// We need to guess the size...
							ASSERT(wsi_image->codeblock_count > 0);
							seektable_size = sizeof(isyntax_seektable_codeblock_header_t) * wsi_image->codeblock_count;
						}
						isyntax_seektable_codeblock_header_t* seektable =
								(isyntax_seektable_codeblock_header_t*) malloc(seektable_size);
						fread(seektable, seektable_size, 1, fp);

						// Now fill in the missing data.
						// NOTE: The number of codeblock entries in the seektable is much greater than the number of
						// codeblocks that *actually* exist in the file. This means that we have to discard many of
						// the seektable entries.
						// Luckily, we can easily identify the entries that need to be discarded.
						// (They have the data offset (and data size) set to 0.)
						i32 seektable_entry_count = seektable_size / sizeof(isyntax_seektable_codeblock_header_t);

						for (i32 i = 0; i < wsi_image->codeblock_count; ++i) {
							isyntax_codeblock_t* codeblock = wsi_image->codeblocks + i;
							if (codeblock->block_id > seektable_entry_count) {
								ASSERT(!"block ID out of bounds");
								continue;
							}
							isyntax_seektable_codeblock_header_t* seektable_entry = seektable + codeblock->block_id;
							ASSERT(seektable_entry->block_data_offset_header.group == 0x301D);
							ASSERT(seektable_entry->block_data_offset_header.element == 0x2010);
							codeblock->block_data_offset = seektable_entry->block_data_offset;
							codeblock->block_size = seektable_entry->block_size;

#if 0
							// Debug test:
							// Decompress codeblocks in the seektable.
							if (1 || i == wsi_image->codeblock_count-1) {
								// Only parse 'non-empty'/'background' codeblocks
								if (codeblock->block_size > 8 /*&& codeblock->block_data_offset == 129572464*/) {
									debug_read_codeblock_from_file(codeblock, fp);
									isyntax_header_template_t* template = isyntax->header_templates + codeblock->block_header_template_id;
									if (template->waveletcoeff != 3) {
										DUMMY_STATEMENT;
									}
									if (i % 1000 == 0) {
										console_print_verbose("reading codeblock %d\n", i);
									}
									u16* decompressed = isyntax_hulsken_decompress(codeblock, isyntax->block_width, isyntax->block_height, 1);
									if (decompressed) {
#if 0
										FILE* out = fopen("hulskendecompressed4.raw", "wb");
										if(out) {
											fwrite(decompressed, codeblock->decompressed_size, 1, out);
											fclose(out);
										}
#endif
										_aligned_free(decompressed);
									}
								}
							}
#endif

						}
#if 0
						test_output_block_header(wsi_image);
#endif

#if 0


						// inverse wavelet transform test
						i32 codeblock_index = 296166;
						debug_decode_wavelet_transformed_chunk(isyntax, fp, wsi_image, codeblock_index, true);

#endif
						// Create tables for spatial lookup of codeblock from tile coordinates
						for (i32 i = 0; i < wsi_image->num_levels; ++i) {
							isyntax_level_t* level = wsi_image->levels + i;
							// NOTE: Tile entry with codeblock_index == 0 will mean there is no codeblock for this tile (empty/background)
							level->tiles = (isyntax_tile_t*) calloc(1, level->tile_count * sizeof(isyntax_tile_t));
						}
						i32 current_chunk_codeblock_index = 0;
						i32 next_chunk_codeblock_index = 0;
						for (i32 i = 0; i < wsi_image->codeblock_count; ++i) {
							isyntax_codeblock_t* codeblock = wsi_image->codeblocks + i;
							if (codeblock->color_component != 0) {
								// Don't let color channels 1 and 2 overwrite what was already set
								i = next_chunk_codeblock_index; // skip ahead
								codeblock = wsi_image->codeblocks + i;
								if (i >= wsi_image->codeblock_count) break;
							}
							// Keep track of where we are in the 'chunk' of codeblocks
							if (i == next_chunk_codeblock_index) {
								i32 chunk_codeblock_count;
								if (codeblock->scale == wsi_image->num_levels-1) {
									chunk_codeblock_count = isyntax_get_chunk_codeblocks_per_color_for_level(codeblock->scale, true) * 3;
								} else {
									chunk_codeblock_count = 21*3;
								}
								current_chunk_codeblock_index = i;
								next_chunk_codeblock_index = i + chunk_codeblock_count;
							}
							isyntax_level_t* level = wsi_image->levels + codeblock->scale;
							i32 tile_index = codeblock->block_y * level->width_in_tiles + codeblock->block_x;
							ASSERT(tile_index < level->tile_count);
							level->tiles[tile_index].codeblock_index = i;
							level->tiles[tile_index].codeblock_chunk_index = current_chunk_codeblock_index;

						}


						parse_ticks_elapsed += (get_clock() - parse_begin);
//						console_print("iSyntax: the seektable is %u bytes, or %g%% of the total file size\n", seektable_size, (float)((float)seektable_size * 100.0f) / isyntax->filesize);
//						console_print("   I/O time: %g seconds\n", get_seconds_elapsed(0, io_ticks_elapsed));
//						console_print("   Parsing time: %g seconds\n", get_seconds_elapsed(0, parse_ticks_elapsed));
						console_print("   iSyntax loading time: %g seconds\n", get_seconds_elapsed(load_begin, get_clock()));
					} else {
						// TODO: error
					}
				}
			} else {
				// TODO: error
			}


			// TODO: further implement iSyntax support
			success = true;

			fail_1:
			free(read_buffer);
		}
		fclose(fp);

		if (success) {
#if WINDOWS
			// TODO: make async I/O platform agnostic
			// TODO: set FILE_FLAG_NO_BUFFERING for maximum performance (but: need to align read requests to page size...)
			// http://vec3.ca/using-win32-asynchronous-io/
			isyntax->win32_file_handle = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
			                                         FILE_ATTRIBUTE_NORMAL | /*FILE_FLAG_SEQUENTIAL_SCAN |*/
			                                         /*FILE_FLAG_NO_BUFFERING |*/ FILE_FLAG_OVERLAPPED,
			                                         NULL);
#else
			tiff->fd = open(filename, O_RDONLY);
				if (tiff->fd == -1) {
					console_print_error("Error: Could not reopen %s for asynchronous I/O\n");
					return false;
				} else {
					// success
				}

#endif
		}
	}
	return success;
}

void isyntax_destroy(isyntax_t* isyntax) {
	for (i32 image_index = 0; image_index < isyntax->image_count; ++image_index) {
		isyntax_image_t* isyntax_image = isyntax->images + image_index;
		if (isyntax_image->image_type == ISYNTAX_IMAGE_TYPE_WSI) {
			if (isyntax_image->codeblocks) {
				free(isyntax_image->codeblocks);
				isyntax_image->codeblocks = NULL;
			}
			for (i32 i = 0; i < isyntax_image->num_levels; ++i) {
				isyntax_level_t* level = isyntax_image->levels + i;
				if (level->tiles) {
					free(level->tiles);
					level->tiles = NULL;
				}
			}
		}
	}
#if WINDOWS
	if (isyntax->win32_file_handle) {
		CloseHandle(isyntax->win32_file_handle);
	}
#else
	if (isyntax->fd) {
		close(isyntax->fd);
	}
#endif
}
