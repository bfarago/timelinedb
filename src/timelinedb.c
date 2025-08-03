/*
    File: timelinedb.c
    This file implements the core functions for handling raw timeline values, including allocation, sample rate conversion, and aggregation.
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
#include "timelinedb.h"
#include "timelinedb_simd.h"

const TimelineBackendFunctions *g_TimelineBackendFunctions = &gTimelineBackendFunctionsC;

// -------------------------------------
static inline void* alloc_raw_aligned(size_t size, uint8_t alignment) {
    void *ptr = NULL;
    if (alignment <= 1) {
        ptr = malloc(size);
    } else {
        size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);
        ptr = aligned_alloc(alignment, aligned_size);
    }
    if (!ptr) {
        fprintf(stderr, "ERROR: Memory allocation failed for size %zu\n", size);
        return NULL;
    }
    return ptr;
}

void init_RawTimelineValuesBuf(RawTimelineValuesBuf *buf) {
    if (buf) {
        buf->nr_of_samples = 0;
        buf->nr_of_channels = 0;
        buf->bitwidth = 0;
        // buf->bytealignment = 0;
        buf->bytes_per_sample = 0;
        buf->value_type = TR_undefined;
        buf->time_exponent = 0;
        buf->time_step = 0;
        buf->buffer_size = 0;
        buf->valueBuffer = NULL;
        buf->sample_rate_info = NULL; // This will be set when preparing the buffer for sample rate conversion
        buf->prepared_data_src = NULL; // This will be set when preparing the buffer for sample rate conversion
    }
}
void free_RawTimelineValuesBuf(RawTimelineValuesBuf *buf) {
    if (!buf) return;
    if (buf->valueBuffer) {
        free(buf->valueBuffer);
        buf->valueBuffer = NULL;
    }
    if (buf->prepared_data_src) {
        free(buf->prepared_data_src);
        buf->prepared_data_src = NULL;
    }
    if (buf->sample_rate_info) {
        free(buf->sample_rate_info);
        buf->sample_rate_info = NULL;
    }
    buf->nr_of_samples = 0;
}

uint8_t getBackendsCount() {
    return 2; // Currently only one backend
}
int getBackendName(uint8_t index, const char **name) {
    if (!name) {
        return -1; // Invalid index
    }
    switch (index) {
    case 0:
        *name = "C Backend";
        break;
    case 1:
        *name = "SIMD Backend";
        break;
    default:
        *name = g_TimelineBackendFunctions->name;
    }
    return 0;
}
int setBackend(uint8_t index) {
    if (index > 1) {
        return -1; // Invalid index
    }
    switch (index) {
    case 0:
        g_TimelineBackendFunctions = &gTimelineBackendFunctionsC;
        break;
    case 1:
        g_TimelineBackendFunctions = &gTimelineBackendFunctionsSIMD;
        break;
    default:
        return -1; // Invalid index
    }
    return 0;
}

static inline int getSampleByteOffset(const RawTimelineValuesBuf *buf, uint32_t sample_index, uint8_t channel, uint32_t *offset) {
    if (!buf || sample_index >= buf->nr_of_samples || channel >= buf->nr_of_channels) {
        return -1; // Invalid access
    }
    *offset= (sample_index * buf->bytes_per_sample)+ (channel * buf->bitwidth / 8);
    return 0;
}

int getSampleValue_int8(const RawTimelineValuesBuf *buf, uint32_t sample_index, uint8_t channel, int8_t *value) {
    if (!buf || !value || sample_index >= buf->nr_of_samples || channel >= buf->nr_of_channels || buf->bitwidth != 8) {
        return -1; // Invalid access
    }
    uint32_t offset;
    if (getSampleByteOffset(buf, sample_index, channel, &offset) != 0) {
        return -1; // Invalid sample access
    }
    *value = *(int8_t*)(&buf->valueBuffer[offset]);
    return 0;
}

int getSampleValue_float32(const RawTimelineValuesBuf *buf, uint32_t sample_index, uint8_t channel, float *value) {
    if (!buf || !value || sample_index >= buf->nr_of_samples || channel >= buf->nr_of_channels || buf->bitwidth != 32) {
        return -1; // Invalid access
    }
    uint32_t offset;
    if (getSampleByteOffset(buf, sample_index, channel, &offset) != 0) {
        return -1; // Invalid sample access
    }
    *value = *(float*)(&buf->valueBuffer[offset]);
    return 0;
}
int getSampleValue_SIMD_sint16x8(const RawTimelineValuesBuf *buf, uint32_t sample_index, uint8_t channel, int16_t *value) {
    if (!buf || !value || sample_index >= buf->nr_of_samples || channel >= buf->nr_of_channels || buf->bitwidth != 16) {
        return -1; // Invalid access
    }
    uint32_t offset;
    if (getSampleByteOffset(buf, sample_index, channel, &offset) != 0) {
        return -1; // Invalid sample access
    }
    *value = *(int16_t*)(&buf->valueBuffer[offset]);
    return 0;
}
void alloc_RawTimelineValuesBuf(RawTimelineValuesBuf *buf, uint32_t nr_of_samples, uint8_t nr_of_channels, uint8_t bitwidth, uint8_t bytealignment, RawTimelineValueEnum value_type) {
    if (!buf) return;
    buf->nr_of_samples = nr_of_samples;
    buf->nr_of_channels = nr_of_channels;
    buf->bitwidth = bitwidth;
    buf->bytes_per_sample = (nr_of_channels*bitwidth+7)/8; // this is not for the array, but for the value elements.
    buf->value_type = value_type;
    buf->buffer_size = nr_of_samples * nr_of_channels * buf->bytes_per_sample;

//    printf("Allocating RawTimelineValuesBuf: %u samples, %u channels, %u bytes/sample, total size: %u bytes\n",
//           nr_of_samples, nr_of_channels, buf->bytes_per_sample, buf->buffer_size);
    buf->valueBuffer = alloc_raw_aligned(buf->buffer_size, bytealignment);
    if (!buf->valueBuffer) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }
}
void getEngineeringSampleRateFrequency(const RawTimelineValuesBuf *buf, double *freq_val, const char **freq_unit) {
    static const char *units[] = {"Hz", "kHz", "MHz", "GHz", "THz", "PHz"};
    double freq_hz = 1.0 / (buf->time_step * pow(10.0, buf->time_exponent));
    int exponent_index = 0;

    while (freq_hz >= 1000.0 && exponent_index < 5) {
        freq_hz /= 1000.0;
        exponent_index++;
    }

    *freq_val = freq_hz;
    *freq_unit = units[exponent_index];
}
void getEngineeringTimeInterval(const RawTimelineValuesBuf *buf, double *time_val, const char **time_unit) {
    int time_exp = buf->time_exponent;
    switch (time_exp) {
        case 0:  *time_unit = "s"; break;
        case -3: *time_unit = "ms"; break;
        case -6: *time_unit = "us"; break;
        case -9: *time_unit = "ns"; break;
        case -12: *time_unit = "ps"; break;
        case -15: *time_unit = "fs"; break;
        default: *time_unit = "?s"; break;
    }
    *time_val = buf->time_step;
}

int prepare_SampleRateConversion(const RawTimelineValuesBuf *input, uint32_t new_sample_rate_hz, RawTimelineValuesBuf *output) {
    if (!input || !output) return -1;

    double time_unit = pow(10.0, input->time_exponent);
    double old_rate = 1.0 / (input->time_step * time_unit);
    double rate_ratio = (double)new_sample_rate_hz / old_rate;
    uint32_t new_nr_samples = (uint32_t)(input->nr_of_samples * rate_ratio);

    double ideal_time = 1.0 / new_sample_rate_hz;
    int8_t exp = 0;
    uint32_t step = 0;
    for (int e = 15; e >= -15; e -= 3) {
        double base_unit = pow(10.0, e);
        double candidate_step = ideal_time / base_unit;
        if (candidate_step >= 1.0 && candidate_step <= UINT32_MAX) {
            exp = e;
            step = (uint32_t)(candidate_step + 0.5);
            break;
        }
    }

    output->time_exponent = exp;
    output->time_step = step;

    //later we can add more commmon sample parameters
    output->sample_rate_info = (SampleRateInfo*)malloc(sizeof(SampleRateInfo));
    if (!output->sample_rate_info) {
        fprintf(stderr, "Memory allocation failed for SampleRateInfo\n");
        return -1;
    }
    /*
    double in_time_unit = pow(10.0, input->time_exponent);
    double out_time_unit = pow(10.0, output->time_exponent);
    double in_sample_time = input->time_step * in_time_unit;
    double out_sample_time = output->time_step * out_time_unit;
    double rate2 = in_sample_time / out_sample_time;
    */
    output->sample_rate_info->rate_ratio = rate_ratio;
    
    alloc_RawTimelineValuesBuf(output, new_nr_samples, input->nr_of_channels, input->bitwidth, input->bytes_per_sample, input->value_type);
    if (output->value_type == TR_SIMD_sint16x8) {
        if (output->prepared_data_src) {
            free(output->prepared_data_src);
            output->prepared_data_src = NULL;
        }
        init_InterpInfo(input, output);
    }
    return (output->valueBuffer == NULL) ? -1 : 0;
}

int convert_sample_rate_analog_sint8(const RawTimelineValuesBuf *input, RawTimelineValuesBuf *output, double rate_ratio, uint32_t new_nr_samples) {
    for (uint32_t i = 0; i < new_nr_samples; ++i) {
        double original_index = (double)i / rate_ratio;
        uint32_t index_lower = (uint32_t)floor(original_index);
        uint32_t index_upper = (index_lower + 1 < input->nr_of_samples) ? index_lower + 1 : index_lower;
        double frac = original_index - index_lower;

        for (uint8_t ch = 0; ch < input->nr_of_channels; ++ch) {
            int8_t v1 = ((int8_t*)input->valueBuffer)[index_lower * input->nr_of_channels + ch];
            int8_t v2 = ((int8_t*)input->valueBuffer)[index_upper * input->nr_of_channels + ch];
            double interpolated = (1.0 - frac) * v1 + frac * v2;
            ((int8_t*)output->valueBuffer)[i * input->nr_of_channels + ch] = (int8_t)(round(interpolated));
        }
    }
    return 0;
}

int convert_sample_rate(const RawTimelineValuesBuf *input, RawTimelineValuesBuf *output) {
    if (!input || !output) {
        fprintf(stderr, "Unsupported or invalid input\n");
        return -1;
    }

    if ( input->value_type == TR_analog_sint8) {
        return convert_sample_rate_analog_sint8(input, output, output->sample_rate_info->rate_ratio, output->nr_of_samples);
    } else if (input->value_type == TR_SIMD_sint16x8) {
        return g_TimelineBackendFunctions->convert_sample_rate_s16x8(input, output);
    } else {
        fprintf(stderr, "Unsupported value type for sample rate conversion\n");
        return -1;
    }
}

int prepare_NeonAlignedBuffer(const RawTimelineValuesBuf *src, RawTimelineValuesBuf *dst) {
    if (!src || !dst || src->value_type != TR_analog_sint8 || src->bitwidth != 8) {
        return -1;
    }
    uint8_t channels = 8; // src->nr_of_channels;
    //if (channels > 8) channels = 8; // Limit to 8 channels for SIMD
    // uint32_t samples = src->nr_of_samples* channels;
    dst->time_exponent = src->time_exponent;
    dst->time_step = src->time_step;
    dst->value_type = TR_SIMD_sint16x8;
    dst->nr_of_samples = src->nr_of_samples;
    dst->nr_of_channels = channels;
    //byte alignment is always 16 for SIMD
    alloc_RawTimelineValuesBuf(dst, src->nr_of_samples, channels, 16, 16, TR_SIMD_sint16x8);
    if (!dst->valueBuffer) return -1;
    return 0;
}

int convert_to_NeonAlignedBuffer(const RawTimelineValuesBuf *src, RawTimelineValuesBuf *dst, uint8_t srcChannel, uint8_t dstChannel) {
    if (src->value_type != TR_analog_sint8 || src->bitwidth != 8) {
        return -1; // Unsupported input type
    }
    if (!dst || dst->value_type != TR_SIMD_sint16x8 || dst->bitwidth != 16) {
        return -1; // Unsupported or invalid output type
    }
    if (dst->nr_of_samples != src->nr_of_samples || dst->nr_of_channels > 8) {
        return -1; // Mismatch in sample count or too many channels
    }
   
    for (uint32_t i = 0; i < src->nr_of_samples; ++i) {
        uint32_t srcSampleOffset;
        if (getSampleByteOffset(src, i, srcChannel, &srcSampleOffset) != 0) {
            return -1; // Invalid sample access
        }
        uint32_t dstSampleOffset;
        if (getSampleByteOffset(dst, i, dstChannel, &dstSampleOffset) != 0) {
            return -1; // Invalid sample access
        }
        int8_t srcData= *(int8_t*)&src->valueBuffer[srcSampleOffset];
        int16_t *simd_data = (int16_t*)&dst->valueBuffer[dstSampleOffset];
        *simd_data = srcData;
    }

    return 0;
}

int convert_from_NeonAlignedBuffer(const RawTimelineValuesBuf *src, RawTimelineValuesBuf *dst) {
    if (src->value_type != TR_SIMD_sint16x8 || src->bitwidth != 16) {
        return -1; // Unsupported input type
    }
    if (!dst || dst->value_type != TR_analog_sint8 || dst->bitwidth != 8) {
        return -1; // Unsupported or invalid output type
    }
    int16_t *src_array = (int16_t*)src->valueBuffer;
    int8_t *dst_array = (int8_t*)dst->valueBuffer;
    for (uint32_t i = 0; i < src->nr_of_samples; ++i) {
        uint32_t srcSampleOffset;
        if (getSampleByteOffset(src, i, 0, &srcSampleOffset) != 0) {
            return -1; // Invalid sample access
        }
        uint32_t dstSampleOffset;
        if (getSampleByteOffset(dst, i, 0, &dstSampleOffset) != 0) {
            return -1; // Invalid sample access
        }
        int16_t srcData = src_array[srcSampleOffset];
        dst_array[dstSampleOffset] = (int8_t)(srcData);
    }
    dst->nr_of_samples = src->nr_of_samples;
    return 0;
}

int prepare_AggregationMinMax(const RawTimelineValuesBuf *input, RawTimelineValuesBuf *outMin, RawTimelineValuesBuf *outMax, uint32_t outSampleNr) {
    if (!input || !outMin || !outMax) {
        return -1; // Invalid input
    }
    if (input->value_type != TR_analog_sint8 && input->value_type != TR_SIMD_sint16x8) {
        return -1; // Unsupported value type
    }

    outMin->time_exponent = input->time_exponent;
    outMin->time_step = input->time_step;
    outMin->value_type = input->value_type;
    outMin->nr_of_samples = outSampleNr;
    outMin->nr_of_channels = input->nr_of_channels;
    alloc_RawTimelineValuesBuf(outMin, outSampleNr, input->nr_of_channels, input->bitwidth, input->bytes_per_sample, input->value_type);
    
    outMax->time_exponent = input->time_exponent;
    outMax->time_step = input->time_step;
    outMax->value_type = input->value_type;
    outMax->nr_of_samples = outSampleNr;
    outMax->nr_of_channels = input->nr_of_channels;
    alloc_RawTimelineValuesBuf(outMax, outSampleNr, input->nr_of_channels, input->bitwidth, input->bytes_per_sample, input->value_type);

    return (outMin->valueBuffer && outMax->valueBuffer) ? 0 : -1;
}

int aggregate_MinMax(const RawTimelineValuesBuf *input, RawTimelineValuesBuf *outMin, RawTimelineValuesBuf *outMax, uint32_t inSamples, uint32_t inOffset) {
    if (!input || !outMin || !outMax) {
        return -1; // Invalid input
    }
    if (input->value_type != TR_analog_sint8 && input->value_type != TR_SIMD_sint16x8) {
        return -1; // Unsupported value type
    }
    // choose fn based on config and buffer type.
    fn_aggregate_minmax minmax_fn;
    if (input->value_type == TR_analog_sint8) {
        minmax_fn = g_TimelineBackendFunctions->aggregate_minmax_s8;
    } else if (input->value_type == TR_SIMD_sint16x8) {
        minmax_fn = g_TimelineBackendFunctions->aggregate_minmax_s16x8;
    } else {
        fprintf(stderr, "Unsupported value type for aggregation\n");
        return -1; // Unsupported value type
    }
    // run multiple passes to aggregate min and max values
    uint32_t in_samples = (inSamples > 0) ? inSamples : input->nr_of_samples;
    uint32_t out_samples = outMin->nr_of_samples;
    float stride_f = (float)in_samples / (float)out_samples;
    for (uint32_t i = 0; i < out_samples; ++i) {
        uint32_t start = inOffset + (uint32_t)floorf(i * stride_f);
        uint32_t end = inOffset + (uint32_t)floorf((i + 1) * stride_f);
        if (end <= start) end = start + 1;
        if (end > inOffset + in_samples) end = inOffset + in_samples;
        minmax_fn(input, outMin, outMax, i, start, end);
    }
    return 0;
}
