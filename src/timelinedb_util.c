/*
    File: timelinedb_util.c
    This file contains some util functions like a hex dump and sine wave generation for testing.
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
#include "timelinedb_util.h"
#define PI 3.14159265358979323846

void dump_RawTimelineValuesBuf(const RawTimelineValuesBuf *buf) {
    const char *freq_unit, *time_unit;
    double freq_val;
    double time_val;
    getEngineeringSampleRateFrequency(buf, &freq_val, &freq_unit);
    getEngineeringTimeInterval(buf, &time_val, &time_unit);
    printf("Dumping timeline buffer: %u samples, buf_size=%u, bitwidth=%u, align=%u, timestep=%u * 10^%d (~%.0f %s), sample rate: ~%.3f %s:\n",
           buf->nr_of_samples, buf->buffer_size, buf->bitwidth, buf->bytes_per_sample,
           buf->time_step, buf->time_exponent, time_val, time_unit, 
           freq_val, freq_unit
           );
    for(uint8_t ch = 0; ch < buf->nr_of_channels; ch++) {
        printf("Ch[%d]: ", ch);
        switch (buf->value_type) {
            case TR_analog_sint8:
                for (uint32_t i = 0; i < buf->nr_of_samples; i++) {
                    int8_t value;
                    if (getSampleValue_int8(buf, i, ch, &value) == 0) {
                        printf("%4d ", value);
                    } else {
                        printf("?? ");
                    }
                }
                break;
            case TR_digital8:
                for (uint32_t i = 0; i < buf->nr_of_samples; i++) {
                    uint8_t value;
                    if (getSampleValue_int8(buf, i, ch, (int8_t*)&value) == 0) {
                        printf("0x%02X ", value);
                    } else {
                        printf("?? ");
                    }
                }
                break;
            case TR_SIMD_sint16x8:
                for (uint32_t i = 0; i < buf->nr_of_samples; i++) {
                    int16_t value;
                    if (getSampleValue_SIMD_sint16x8(buf, i, ch, &value) == 0) {
                        printf("%4d ", value);
                    } else {
                        printf("?? ");
                    }
                }
                break;
            default:
                printf("Unknown type\n");
                break;
        }
        printf("\n");
    }
    //printf("\n");
}

void generate_sine_wave(RawTimelineValuesBuf *buf, uint32_t num_samples, uint8_t num_channels, float period, float amplitude, uint32_t sample_rate_hz) {
    buf->nr_of_samples = num_samples;
    buf->nr_of_channels = num_channels;
    int exponent = 0;
    uint32_t scaled_rate = sample_rate_hz;
    while (scaled_rate >= 1000) {
        scaled_rate /= 1000;
        exponent += 3;
    }
    buf->time_exponent = -exponent;
    buf->time_step = sample_rate_hz / (uint32_t)pow(10, exponent);
    // Set buffer_size as per new signature
    buf->buffer_size = num_samples * num_channels * buf->bytes_per_sample;

    if (buf->value_type == TR_SIMD_sint16x8) {
        buf->bitwidth = 16;
        buf->bytes_per_sample = sizeof(int16_t) * 8;
        buf->buffer_size = num_samples * buf->bytes_per_sample;
        buf->valueBuffer = aligned_alloc(16, buf->buffer_size);
        if (!buf->valueBuffer) {
            fprintf(stderr, "Memory allocation failed\n");
            exit(1);
        }

        int16_t *data = (int16_t *)buf->valueBuffer;
        for (uint32_t i = 0; i < num_samples; i++) {
            for (uint8_t ch = 0; ch < 8; ch++) {
                float t = ((float)i + (float)ch / 8.0f) / period;
                float val = amplitude * sinf(2 * PI * t);
                int16_t sval = (int16_t)(val > 32767 ? 32767 : val < -32768 ? -32768 : val);
                data[i * 8 + ch] = sval;
            }
        }
    }
    else if (buf->value_type == TR_analog_sint8) {
        buf->bitwidth = 8;
        buf->bytes_per_sample = 1;
        buf->buffer_size = num_samples * num_channels;
        buf->valueBuffer = malloc(buf->buffer_size);
        if (!buf->valueBuffer) {
            fprintf(stderr, "Memory allocation failed\n");
            exit(1);
        }

        uint8_t *data = (uint8_t *)buf->valueBuffer;
        for (uint32_t i = 0; i < num_samples; i++) {
            for (uint8_t ch = 0; ch < num_channels; ch++) {
                float t = ((float)i + (float)ch / num_channels) / period;
                float val = amplitude * sinf(2 * PI * t);
                int8_t sval = (int8_t)(val > 127 ? 127 : val < -128 ? -128 : val);
                data[i * num_channels + ch] = (uint8_t)sval;
            }
        }
    }
    else {
        fprintf(stderr, "Unsupported value type for sine wave generation\n");
        free(buf->valueBuffer);
        exit(1);
    }
}
