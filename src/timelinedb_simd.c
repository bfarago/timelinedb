/*
    File: timelinedb_simd.c
    This file implements sample rate conversion and aggregation functions using SIMD (Single Instruction, Multiple Data) technology.
    Author: Barna Farago - MYND-Ideal kft.
    Date: 2025-07-01
    License: Modified MIT License. You can use it for learn, but I can sell it as closed source with some improvements...
*/
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stddef.h>
#include "timelinedb_simd.h"



/*
Note: Some quick performance test was made with this preliminary sources on Apple M2 Pro.
Sample rate conversion from 1MHz to 100kHz, 8 channels, 16-bit signed integer audio data.
-O3 compiler optimization was used to compile this code, which is necessary for even SIMD implementation's performance.
C Backend sample rate conversion took 16673 microseconds
Neon SIMD Backend sample rate conversion took 2549 microseconds
=> ~6.5x speedup with NEON SIMD
With different optimization levels, the results may vary:
C Backend sample rate conversion took 14504 microseconds
Neon SIMD Backend sample rate conversion took 2789 microseconds
=> ~5.2x speedup with NEON SIMD
This testcase was run with 1000000 samples, 8 channels, 16-bit signed integer audio data converted to 150000 samples. 1.6Mbyte input and 240kbyte output.
Aprox. 5.2Mbyte was moved in N ms, which is aprox 1.4GB/s RAM throughput (using the L1 cache as well).

Without opt, like -O0, the performance is terrible, C Backend version even quicker then SIMD but both are slow, aprox 20 times.
*/

/* SAMPLE RATE CONVERSION
    This function converts the sample rate of a RawTimelineValuesBuf from one rate to another.
    It uses linear interpolation for the conversion.
    The input is expected to be a RawTimelineValuesBuf with 16-bit signed integer samples.
    The output will be a new RawTimelineValuesBuf with the converted sample rate.
    The function returns 0 on success, or -1 on failure (e.g., if the number of channels is not 8).
*/
static int convert_sample_rate_SIMD_s16x8_c(const RawTimelineValuesBuf *input, RawTimelineValuesBuf *output) {
    int16_t *src = (int16_t*)input->valueBuffer;
    int16_t *dst = (int16_t*)output->valueBuffer;
    uint32_t ch = input->nr_of_channels;
    if (ch != 8) return -1;
    uint32_t new_nr_samples = output->nr_of_samples;

    double rate_ratio = output->sample_rate_info->rate_ratio;

    for (uint32_t i = 0; i < new_nr_samples; ++i) {
        double original_index = (double)i / rate_ratio;
        uint32_t idx0 = (uint32_t)original_index;
        if (idx0 >= input->nr_of_samples-1) idx0 = input->nr_of_samples - 2; // Clamp to last sample
         
        uint32_t idx1 = (idx0 + 1 < input->nr_of_samples) ? idx0 + 1 : idx0;
        double frac = original_index - idx0;

        for (uint8_t j = 0; j < ch; ++j) {
            int16_t v0 = src[idx0 * ch + j];
            int16_t v1 = src[idx1 * ch + j];
            double interp = (1.0 - frac) * v0 + frac * v1;
            dst[i * ch + j] = (int16_t)(round(interp));
        }
    }
    return 0;
}

#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(NEON_ENABLED)
/*
static int convert_sample_rate_SIMD_s16x8_neon(const RawTimelineValuesBuf *input, RawTimelineValuesBuf *output, double rate_ratio, uint32_t new_nr_samples) {
    int16_t *src = (int16_t*)input->valueBuffer;
    int16_t *dst = (int16_t*)output->valueBuffer;
    uint32_t ch = input->nr_of_channels;
    if (ch != 8) return -1;

    // Precompute fixed-point step in Q16.16
    uint32_t step_fixed = (uint32_t)(65536.0 / rate_ratio);
    uint32_t pos_fixed = 0;

    for (uint32_t i = 0; i < new_nr_samples; ++i) {
        uint32_t idx0 = pos_fixed >> 16;
        uint32_t idx1 = (idx0 + 1 < input->nr_of_samples) ? idx0 + 1 : idx0;
        uint32_t frac_fixed = pos_fixed & 0xFFFF;
        uint32_t inv_frac_fixed = 0x10000 - frac_fixed;

        int16x8_t v0 = vld1q_s16(&src[idx0 * ch]);
        int16x8_t v1 = vld1q_s16(&src[idx1 * ch]);

        int32x4_t v0_lo = vmovl_s16(vget_low_s16(v0));
        int32x4_t v0_hi = vmovl_s16(vget_high_s16(v0));
        int32x4_t v1_lo = vmovl_s16(vget_low_s16(v1));
        int32x4_t v1_hi = vmovl_s16(vget_high_s16(v1));

        int32x4_t interp_lo = vmlaq_n_s32(vmulq_n_s32(v0_lo, inv_frac_fixed), v1_lo, frac_fixed);
        int32x4_t interp_hi = vmlaq_n_s32(vmulq_n_s32(v0_hi, inv_frac_fixed), v1_hi, frac_fixed);

        // Normalize by shifting down from Q16.16 to Q0
        interp_lo = vrshrq_n_s32(interp_lo, 16);
        interp_hi = vrshrq_n_s32(interp_hi, 16);

        int16x4_t res_lo = vmovn_s32(interp_lo);
        int16x4_t res_hi = vmovn_s32(interp_hi);
        int16x8_t result = vcombine_s16(res_lo, res_hi);

        vst1q_s16(&dst[i * ch], result);

        pos_fixed += step_fixed;
    }
    return 0;
}
*/
int init_InterpInfo(const RawTimelineValuesBuf *input, RawTimelineValuesBuf *output) {
    int16_t *src = (int16_t*)input->valueBuffer;
    int16_t *dst = (int16_t*)output->valueBuffer;
    uint32_t ch = input->nr_of_channels;
    if (ch != 8) return -1;
    uint32_t new_nr_samples=output->nr_of_samples;

    SampleInterpInfo *interp_array = (SampleInterpInfo*)malloc(new_nr_samples * sizeof(SampleInterpInfo));
    output->prepared_data_src = interp_array;

    for (uint32_t i = 0; i < new_nr_samples; ++i) {
        double original_index = (double)i / ((double)new_nr_samples / (double)input->nr_of_samples);
        uint32_t idx0 = (uint32_t)original_index;
        if (idx0 >= input->nr_of_samples - 1) idx0 = input->nr_of_samples - 2;
        uint32_t idx1 = (idx0 + 1 < input->nr_of_samples) ? idx0 + 1 : idx0;
        double frac = original_index - idx0;
        uint16_t frac_fixed = (uint16_t)(frac * 65536.0);
        uint16_t inv_frac_fixed = 0x10000 - frac_fixed;
        interp_array[i].idx0 = idx0;
        interp_array[i].idx1 = idx1;
        interp_array[i].frac = frac_fixed;
        interp_array[i].inv_frac = inv_frac_fixed;
    }
    return 0;
}
void free_InterpInfo(RawTimelineValuesBuf *output) {
    if (output->prepared_data_src) {
        free(output->prepared_data_src);
        output->prepared_data_src = NULL;
    }
}

static int convert_sample_rate_SIMD_s16x8_neon(
    const RawTimelineValuesBuf *input, RawTimelineValuesBuf *output)
{
    int16_t *src = (int16_t*)input->valueBuffer;
    int16_t *dst = (int16_t*)output->valueBuffer;
    uint32_t ch = input->nr_of_channels;

    uint32_t new_nr_samples = output->nr_of_samples;
    SampleInterpInfo *interp_array = (SampleInterpInfo *)output->prepared_data_src;
    if (1){
        for (uint32_t i = 0; i + 1 < new_nr_samples; i++) {
            const SampleInterpInfo *p0 = &interp_array[i];
            uint16_t p0frac = p0->frac;
            uint16_t p0inv_frac = p0->inv_frac;
            
            int16x8_t v0_0 = vld1q_s16(&src[p0->idx0 * ch]);
            int16x8_t v1_0 = vld1q_s16(&src[p0->idx1 * ch]);
            int32x4_t interp_lo_0 = vmlaq_n_s32(vmulq_n_s32(vmovl_s16(vget_low_s16(v0_0)), p0inv_frac), vmovl_s16(vget_low_s16(v1_0)), p0frac);
            int32x4_t interp_hi_0 = vmlaq_n_s32(vmulq_n_s32(vmovl_s16(vget_high_s16(v0_0)), p0inv_frac), vmovl_s16(vget_high_s16(v1_0)), p0frac);
            int16x8_t result_0 = vcombine_s16(vmovn_s32(vrshrq_n_s32(interp_lo_0, 16)), vmovn_s32(vrshrq_n_s32(interp_hi_0, 16)));
            vst1q_s16(&dst[i * ch], result_0);
        }
    } else {
        /* This is a more optimized version that processes 4 samples at a time
        // but actually it is not better than the loop version above.
        // which is strange, it should be better. It is on invenstigation yet.
        */
        int16x8_t* optr = (int16x8_t*)dst;
        for (uint32_t i = 0; i + 1 < new_nr_samples; i += 4) {
            const SampleInterpInfo *p0 = &interp_array[i];
            uint16_t p0frac = p0->frac;
            uint16_t p0inv_frac = p0->inv_frac;
            uint16_t p0idx0 = p0->idx0 *8;
            int16_t *base_ptr = &src[p0idx0];
            p0++;
            uint16_t p1frac = p0->frac;
            uint16_t p1inv_frac = p0->inv_frac;
            int16x8_t v0, v1;
            int16x8x2_t v_pair0 = vld1q_s16_x2(base_ptr); // Loads 2 x 16 bytes (32 bytes total)
            int16x8x2_t v_pair1 = vld1q_s16_x2(base_ptr+8); // Loads 2 x 16 bytes (32 bytes total)

            p0++;
            uint16_t p2frac = p0->frac;
            uint16_t p2inv_frac = p0->inv_frac;
            uint16_t p2idx0 = p0->idx0 *8;
            int16_t *base_ptr2 = &src[p2idx0];
            p0++;
            uint16_t p3frac = p0->frac;
            uint16_t p3inv_frac = p0->inv_frac;
            int16x8_t v2, v3;
            
            int16x8x2_t v_pair2 = vld1q_s16_x2(base_ptr2); // Loads 2 x 16 bytes (32 bytes total)
            int16x8x2_t v_pair3 = vld1q_s16_x2(base_ptr2+8); // Loads 2 x 16 bytes (32 bytes total)

            v0 = v_pair0.val[0];  // src[p0idx0]
            v1 = v_pair0.val[1];  // src[p0idx1]
            int32x4_t interp_lo_0 = vmlaq_n_s32(vmulq_n_s32(vmovl_s16(vget_low_s16(v0)), p0inv_frac), vmovl_s16(vget_low_s16(v1)), p0frac);
            int32x4_t interp_hi_0 = vmlaq_n_s32(vmulq_n_s32(vmovl_s16(vget_high_s16(v0)), p0inv_frac), vmovl_s16(vget_high_s16(v1)), p0frac);
            int16x8_t result_0 = vcombine_s16(vmovn_s32(vrshrq_n_s32(interp_lo_0, 16)), vmovn_s32(vrshrq_n_s32(interp_hi_0, 16)));
            vst1q_s16((int16_t*)optr, result_0);
            optr++;

            // i + 1
            v0 = v_pair1.val[0];  // src[p0idx0]
            v1 = v_pair1.val[1];  // src[p0idx1]
            int32x4_t interp_lo_1 = vmlaq_n_s32(vmulq_n_s32(vmovl_s16(vget_low_s16(v0)), p1inv_frac), vmovl_s16(vget_low_s16(v1)), p1frac);
            int32x4_t interp_hi_1 = vmlaq_n_s32(vmulq_n_s32(vmovl_s16(vget_high_s16(v0)), p1inv_frac), vmovl_s16(vget_high_s16(v1)), p1frac);
            int16x8_t result_1 = vcombine_s16(vmovn_s32(vrshrq_n_s32(interp_lo_1, 16)), vmovn_s32(vrshrq_n_s32(interp_hi_1, 16)));
            vst1q_s16((int16_t*)optr, result_1);
            optr++;
            
            // i + 2
            v0 = v_pair2.val[0];  // src[p0idx0]
            v1 = v_pair2.val[1];  // src[p0idx1]
            int32x4_t interp_lo_2 = vmlaq_n_s32(vmulq_n_s32(vmovl_s16(vget_low_s16(v0)), p2inv_frac), vmovl_s16(vget_low_s16(v1)), p2frac);
            int32x4_t interp_hi_2 = vmlaq_n_s32(vmulq_n_s32(vmovl_s16(vget_high_s16(v0)), p2inv_frac), vmovl_s16(vget_high_s16(v1)), p2frac);
            int16x8_t result_2 = vcombine_s16(vmovn_s32(vrshrq_n_s32(interp_lo_2, 16)), vmovn_s32(vrshrq_n_s32(interp_hi_2, 16)));
            vst1q_s16((int16_t*)optr, result_2);
            optr++;
            
            // i + 3
            v0 = v_pair3.val[0];  // src[p0idx0]
            v1 = v_pair3.val[1];  // src[p0idx1]
            int32x4_t interp_lo_3 = vmlaq_n_s32(vmulq_n_s32(vmovl_s16(vget_low_s16(v0)), p3inv_frac), vmovl_s16(vget_low_s16(v1)), p3frac);
            int32x4_t interp_hi_3 = vmlaq_n_s32(vmulq_n_s32(vmovl_s16(vget_high_s16(v0)), p3inv_frac), vmovl_s16(vget_high_s16(v1)), p3frac);
            int16x8_t result_3 = vcombine_s16(vmovn_s32(vrshrq_n_s32(interp_lo_3, 16)), vmovn_s32(vrshrq_n_s32(interp_hi_3, 16)));
            vst1q_s16((int16_t*)optr, result_3);
            optr++;
        }
    }
    return 0;
}
#endif

/* AGGREGATION MIN/MAX - Downsample.
    This function computes the minimum and maximum values for each channel in the specified range of samples.
    It is used to downsample the data by aggregating the min and max values over a range of samples.
    The input is expected to be a RawTimelineValuesBuf with 16-bit signed integer samples.
    The output will be two RawTimelineValuesBufs containing the min and max values for each channel.
 */
int aggregate_minmax_SIMD_s16x8_c(const RawTimelineValuesBuf *input, RawTimelineValuesBuf *outMin, RawTimelineValuesBuf *outMax, uint32_t i, uint32_t start, uint32_t end) {
    for (uint8_t ch = 0; ch < input->nr_of_channels; ++ch) {
        int16_t min_val = INT16_MAX;
        int16_t max_val = INT16_MIN;

        for (uint32_t j = start; j < end; ++j) {
            int16_t value;
            if (getSampleValue_SIMD_sint16x8(input, j, ch, &value) == 0) {
                if (value < min_val) min_val = value;
                if (value > max_val) max_val = value;
            } else {
                //fprintf(stderr, "Error accessing sample %u, channel %u\n", j, ch);
                //return -1; // Error accessing sample
            }
        }
        ((int16_t*)outMin->valueBuffer)[i * input->nr_of_channels + ch] = min_val;
        ((int16_t*)outMax->valueBuffer)[i * input->nr_of_channels + ch] = max_val;
    }
    return 0;
}

#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(NEON_ENABLED)
int aggregate_minmax_SIMD_s16x8_neon(const RawTimelineValuesBuf *input, RawTimelineValuesBuf *outMin, RawTimelineValuesBuf *outMax, uint32_t i, uint32_t start, uint32_t end) {
    for (uint8_t ch = 0; ch < input->nr_of_channels; ++ch) {
        int16x8_t min_val = vdupq_n_s16(INT16_MAX);
        int16x8_t max_val = vdupq_n_s16(INT16_MIN);

        for (uint32_t j = start; j < end; ++j) {
            int16x8_t sample = vld1q_s16(((int16_t*)input->valueBuffer) + (j * input->nr_of_channels + ch));
            min_val = vminq_s16(min_val, sample);
            max_val = vmaxq_s16(max_val, sample);
        }
        vst1q_s16(((int16_t*)outMin->valueBuffer) + (i * input->nr_of_channels + ch), min_val);
        vst1q_s16(((int16_t*)outMax->valueBuffer) + (i * input->nr_of_channels + ch), max_val);
    }
    return 0;
}
#endif

#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(NEON_ENABLED)
int aggregate_minmax_s8_neon(const RawTimelineValuesBuf *input, RawTimelineValuesBuf *outMin, RawTimelineValuesBuf *outMax, uint32_t i, uint32_t start, uint32_t end) {
    for (uint8_t ch = 0; ch < input->nr_of_channels; ++ch) {
        int8x16_t min_vec = vdupq_n_s8(INT8_MAX);
        int8x16_t max_vec = vdupq_n_s8(INT8_MIN);

        for (uint32_t j = start; j < end; j += 16) {
            uint32_t remaining = end - j;
            int8x16_t vals;
            if (remaining >= 16) {
                vals = vld1q_s8(&((int8_t*)input->valueBuffer)[j * input->nr_of_channels + ch]);
            } else {
                int8_t tmp[16] = {0};
                for (uint32_t k = 0; k < remaining; ++k) {
                    tmp[k] = ((int8_t*)input->valueBuffer)[(j + k) * input->nr_of_channels + ch];
                }
                vals = vld1q_s8(tmp);
            }

            min_vec = vminq_s8(min_vec, vals);
            max_vec = vmaxq_s8(max_vec, vals);
        }

        // Reduce vector to scalar min and max using temporary arrays
        int8_t tmp_min[16], tmp_max[16];
        vst1q_s8(tmp_min, min_vec);
        vst1q_s8(tmp_max, max_vec);

        int8_t min_val = tmp_min[0], max_val = tmp_max[0];
        for (int i_tmp = 1; i_tmp < 16; ++i_tmp) {
            if (tmp_min[i_tmp] < min_val) min_val = tmp_min[i_tmp];
            if (tmp_max[i_tmp] > max_val) max_val = tmp_max[i_tmp];
        }

        ((int8_t*)outMin->valueBuffer)[i * input->nr_of_channels + ch] = min_val;
        ((int8_t*)outMax->valueBuffer)[i * input->nr_of_channels + ch] = max_val;
    }
    return 0;
}
#endif

int aggregate_minmax_s8_c(const RawTimelineValuesBuf *input, RawTimelineValuesBuf *outMin, RawTimelineValuesBuf *outMax, uint32_t i, uint32_t start, uint32_t end) {
    for (uint8_t ch = 0; ch < input->nr_of_channels; ++ch) {
        int8_t min_val = INT8_MAX;
        int8_t max_val = INT8_MIN;

        for (uint32_t j = start; j < end; ++j) {
            int8_t value;
            if (getSampleValue_int8(input, j, ch, &value) == 0) {
                if (value < min_val) min_val = value;
                if (value > max_val) max_val = value;
            } else {
                fprintf(stderr, "Error accessing sample %u, channel %u\n", j, ch);
                return -1; // Error accessing sample
            }
        }
        ((int8_t*)outMin->valueBuffer)[i * input->nr_of_channels + ch] = min_val;
        ((int8_t*)outMax->valueBuffer)[i * input->nr_of_channels + ch] = max_val;
    }
    return 0;
}

/*
    Bresenham-style fixed-point sample rate conversion for 8-channel 16-bit signed integer audio.
    This function avoids division in the loop, using an accumulator and step size (like Bresenham's algorithm).
    It performs linear interpolation between nearest samples.
*/
// NEON-optimized Bresenham sample rate conversion for 8x int16
#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(NEON_ENABLED)
int convert_sample_rate_SIMD_s16x8_bresenham_neon(const RawTimelineValuesBuf* input, RawTimelineValuesBuf* output)
{
    int16_t *src = (int16_t*)input->valueBuffer;
    int16_t *dst = (int16_t*)output->valueBuffer;
    uint32_t ch = input->nr_of_channels;
    if (ch != 8) return -1;

    uint32_t in_samples = input->nr_of_samples;
    uint32_t out_samples = output->nr_of_samples;

    // Bresenham/fixed-point accumulators
    uint32_t accum = 0;
    uint32_t step = in_samples;
    uint32_t scale = out_samples;
    uint32_t idx0 = 0;

    for (uint32_t i = 0; i < out_samples; ++i) {
        uint32_t idx1 = (idx0 + 1 < in_samples) ? idx0 + 1 : idx0;
        // Compute frac in Q16
        uint32_t frac_fixed = ((uint64_t)accum << 16) / scale;
        uint32_t inv_frac_fixed = 0x10000 - frac_fixed;

        // Load 8 channels for idx0 and idx1
        int16x8_t v0 = vld1q_s16(&src[idx0 * ch]);
        int16x8_t v1 = vld1q_s16(&src[idx1 * ch]);
        // Widen to 32-bit
        int32x4_t v0_lo = vmovl_s16(vget_low_s16(v0));
        int32x4_t v0_hi = vmovl_s16(vget_high_s16(v0));
        int32x4_t v1_lo = vmovl_s16(vget_low_s16(v1));
        int32x4_t v1_hi = vmovl_s16(vget_high_s16(v1));

        // Interpolate: (v0 * inv_frac + v1 * frac) >> 16
        int32x4_t interp_lo = vmlaq_n_s32(vmulq_n_s32(v0_lo, inv_frac_fixed), v1_lo, frac_fixed);
        int32x4_t interp_hi = vmlaq_n_s32(vmulq_n_s32(v0_hi, inv_frac_fixed), v1_hi, frac_fixed);
        interp_lo = vrshrq_n_s32(interp_lo, 16);
        interp_hi = vrshrq_n_s32(interp_hi, 16);
        int16x8_t result = vcombine_s16(vmovn_s32(interp_lo), vmovn_s32(interp_hi));
        vst1q_s16(&dst[i * ch], result);

        accum += step;
        if (accum >= scale) {
            idx0++;
            accum -= scale;
        }
        if (idx0 >= in_samples - 1) {
            idx0 = in_samples - 2;
            accum = 0;
        }
    }
    return 0;
}
#endif

// C version as fallback and dispatcher
static int convert_sample_rate_SIMD_s16x8_bresenham(const RawTimelineValuesBuf *input, RawTimelineValuesBuf *output)
{
    int16_t *src = (int16_t*)input->valueBuffer;
    int16_t *dst = (int16_t*)output->valueBuffer;
    uint32_t ch = input->nr_of_channels;
    if (ch != 8) return -1;

    uint32_t in_samples = input->nr_of_samples;
    uint32_t out_samples = output->nr_of_samples;

    // Fixed-point step: numerator = in_samples, denominator = out_samples
    uint32_t accum = 0;
    uint32_t step = in_samples;
    uint32_t scale = out_samples;
    uint32_t idx0 = 0;

    for (uint32_t i = 0; i < out_samples; ++i) {
        uint32_t idx1 = (idx0 + 1 < in_samples) ? idx0 + 1 : idx0;
        // frac in [0, 1): how far between idx0 and idx1
        double frac = (double)accum / (double)scale;
        for (uint8_t j = 0; j < ch; ++j) {
            int16_t v0 = src[idx0 * ch + j];
            int16_t v1 = src[idx1 * ch + j];
            double interp = (1.0 - frac) * v0 + frac * v1;
            dst[i * ch + j] = (int16_t)(round(interp));
        }
        accum += step;
        if (accum >= scale) {
            idx0++;
            accum -= scale;
        }
        // Clamp idx0 to avoid reading out of bounds
        if (idx0 >= in_samples - 1) {
            idx0 = in_samples - 2;
            accum = 0;
        }
    }
    return 0;
}

/*
    This file implements the backend functions for the TimelineDB using SIMD technology.
    It provides functions for sample rate conversion and aggregation of min/max values.
    The implementation is designed to be flexible, allowing for different SIMD backends (e.g., NEON, AVX2).
*/
// ---- SIMD Backend Global Config Instance for the virtual funtion table ----
const TimelineBackendFunctions gTimelineBackendFunctionsSIMD = {
#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(NEON_ENABLED)
    .name = "Neon SIMD Backend",
    .convert_sample_rate_s16x8 = convert_sample_rate_SIMD_s16x8_bresenham_neon, // Use dispatcher
    .aggregate_minmax_s8 = aggregate_minmax_s8_neon,
    .aggregate_minmax_s16x8 = aggregate_minmax_SIMD_s16x8_neon,
#elif defined(__AVX2__) || defined(__AVX__)
    .name = "Intel AVX2 SIMD Backend",
    .convert_sample_rate_s16x8 = convert_sample_rate_SIMD_s16x8_avx, // AVX2 fallback
    .aggregate_minmax_s8 = aggregate_minmax_s8_avx, // AVX2 fallback
    .aggregate_minmax_s16x8 = aggregate_minmax_SIMD_s16x8_avx, // AVX2 fallback
#else   //fallback to C version implemented version of SIMD technology is not available or disabled
    .name = "Fallback C Backend",
    .convert_sample_rate_s16x8 = convert_sample_rate_SIMD_s16x8_bresenham,
    .aggregate_minmax_s8 = aggregate_minmax_s8_c,
    .aggregate_minmax_s16x8 = aggregate_minmax_SIMD_s16x8_c,
#endif
};

// ---- SIMD Backend Global Instance for the c version of the funcions ----
const TimelineBackendFunctions gTimelineBackendFunctionsC = {
    .name = "C Backend",
    .convert_sample_rate_s16x8 = convert_sample_rate_SIMD_s16x8_bresenham, //convert_sample_rate_SIMD_s16x8_c,
    .aggregate_minmax_s8 = aggregate_minmax_s8_c,
    .aggregate_minmax_s16x8 = aggregate_minmax_SIMD_s16x8_c,
};
