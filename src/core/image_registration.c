/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2023  Pieter Valkema

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

#include "common.h"
#include "image.h"
#include "image_registration.h"

#include "phasecorrelate.h"

#include "stb_image.h"
#include "stb_image_write.h"

float* convert_image_float_rgb_to_y(float* data, i32 w, i32 h, i32 components) {
    float* result = NULL;
    if (components == 3 || components == 4) {
        result = malloc(w * h * sizeof(float));
        i32 row_elements = w * components;
        for (i32 y = 0; y < h; ++y) {
            float* src_pixel = data + y * row_elements;
            float* dst_pixel = result + y * w;
            for (i32 x = 0; x < w; ++x) {
                float luminance = f32_rgb_to_f32_y(src_pixel[0], src_pixel[1], src_pixel[2]);
//				luminance -= 0.5f; // convert to range [-0.5 - +0.5]
                *dst_pixel++ = luminance;
                src_pixel += components;
            }
        }
    } else {
        printf("number of components (%d) not supported\n", components);
        exit(1);
    }
    return result;
}

#if 0
void run_test_with_basic_test_images() {
    i32 x1 = 0, y1 = 0, comp1 = 0;
    i32 x2 = 0, y2 = 0, comp2 = 0;
    i32 desired_components = 1;
#if 1
    float* raw_data1 = stbi_loadf("patch1.jpg", &x1, &y1, &comp1, desired_components);
    float* raw_data2 = stbi_loadf("patch2.jpg", &x2, &y2, &comp2, desired_components);
#else
    float* raw_data1 = stbi_loadf("horse.jpg", &x1, &y1, &comp1, desired_components);
	float* raw_data2 = stbi_loadf("horse_translated.jpg", &x2, &y2, &comp2, desired_components);
#endif
    if (!(raw_data1 && raw_data2)) {
        printf("input image files not found\n");
        exit(1);
    }

#if 0
    double* luminance_data1 = convert_image_rgb_to_y_double(raw_data1, x1, y1, comp1);
	double* luminance_data2 = convert_image_rgb_to_y_double(raw_data2, x2, y2, comp2);
#else
    float* luminance_data1 = desired_components == 1 ? raw_data1 : convert_image_float_rgb_to_y(raw_data1, x1, y1,
                                                                                                comp1);
    float* luminance_data2 = desired_components == 1 ? raw_data2 : convert_image_float_rgb_to_y(raw_data2, x2, y2,
                                                                                                comp2);
#endif

    buffer2d_t input1 = { .w = x1, .h = y1, .data = luminance_data1};
    buffer2d_t input2 = { .w = x2, .h = y2, .data = luminance_data2};

    phase_correlate(&input1, &input2, NULL, 1.0f, NULL);

    printf("Basic test: success!\n");
}
#endif

static void convert_bgra_to_rgba(uint32_t* pixels, i32 pixel_count) {
    // convert bgr to rgb
    u8* p = (u8*)pixels;
    for (i32 i=0; i < pixel_count; ++i) {
        u8 t = p[0];
        p[0] = p[2];
        p[2] = t;
        p += 4;
    }
}

static void set_white_level(float* pixels, i32 pixel_count, float white) {
    float scale = 1.0f / white;
    for (i32 i = 0; i < pixel_count; ++i) {
        float p = pixels[i];
        p *= scale;
        p = ATMOST(1.0f, p);
        pixels[i] = p;
    }
}

static void set_black_level(float* pixels, i32 pixel_count, float black) {
    float scale = 1.0f / (1.0f - black);
    for (i32 i = 0; i < pixel_count; ++i) {
        float p = pixels[i];
        p -= black;
        p *= scale;
        p = ATLEAST(0.0f, p);
        pixels[i] = p;
    }
}

image_transform_t do_image_registration(image_t* image1, image_t* image2, i32 levels_from_top) {
    image_transform_t result = {};

    if (!(image1->backend == IMAGE_BACKEND_OPENSLIDE && image2->backend == IMAGE_BACKEND_OPENSLIDE)) {
        // TODO: implement read_region() for other image backends
        return result;
    }

    i64 start = get_clock();

    i32 thumb_level1 = ATLEAST(0, image1->level_count - levels_from_top - 1);
    i32 thumb_level2 = ATLEAST(0, image2->level_count - levels_from_top - 1);

    if (thumb_level1 != thumb_level2) {
        console_print("Image registration not possible: number of levels differs (%d vs %d)\n", image1->level_count, image2->level_count);
        return result;
    }

    level_image_t* level_image1 = image1->level_images + thumb_level1;
    level_image_t* level_image2 = image2->level_images + thumb_level2;

    i32 w1 = (i32)level_image1->width_in_pixels;
    i32 h1 = (i32)level_image1->height_in_pixels;
    i32 w2 = (i32)level_image2->width_in_pixels;
    i32 h2 = (i32)level_image2->height_in_pixels;

    float* region1 = calloc(1, w1 * h1 * sizeof(float));
    float* region2 = calloc(1, w2 * h2 * sizeof(float));

    bool ok = image_read_region(image1, thumb_level1, 0, 0, w1, h1, region1, PIXEL_FORMAT_F32_Y);
    ok = ok && image_read_region(image2, thumb_level2, 0, 0, w2, h2, region2, PIXEL_FORMAT_F32_Y);
    if (!ok) {
        console_print("Image registration not possible: image_read_region() failed\n");
        return result;
    }

    i64 clock_after_read = get_clock();

    set_white_level(region1, w1 * h1, 230.0f / 255.0f);
    set_white_level(region2, w2 * h2, 230.0f / 255.0f);

    set_black_level(region2, w2 * h2, 150.0f / 255.0f);

//    void debug_create_luminance_png(real_t* src, i32 w, i32 h, real_t scale, const char* filename);
//    debug_create_luminance_png(region1, w1, h1, 1.0f, "thumb1.png");
//    debug_create_luminance_png(region2, w2, h2, 1.0f, "thumb2.png");

    buffer2d_t input1 = { .w = w1, .h = h1, .data = region1};
    buffer2d_t input2 = { .w = w2, .h = h2, .data = region2};

    v2f pixel_shift = phase_correlate(&input1, &input2, NULL, 1.0f, NULL);
    pixel_shift.x *= image2->mpp_x * level_image2->downsample_factor;
    pixel_shift.y *= image2->mpp_y * level_image2->downsample_factor;

    result.translate = pixel_shift;
    result.is_valid = true;

    console_print("Image registration (on level %d): level0 pixel offset = (%.0f, %.0f), io time = %g seconds, processing time = %g seconds\n",
                  thumb_level1, pixel_shift.x / image2->mpp_x, pixel_shift.y / image2->mpp_y,
                  get_seconds_elapsed(start, clock_after_read), get_seconds_elapsed(clock_after_read, get_clock()));

    free(region1);
    free(region2);
    return result;
}
