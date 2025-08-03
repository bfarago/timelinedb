/*
    File: devtest.c
    This file implements a developer test for the timeline database functions, including sample rate conversion and aggregation.
    Author: Barna Farago - MYND-Ideal kft.
    Date: 2025-07-01
    License: Modified MIT License. You can use it for learn, but I can sell it as closed source with some improvements...
*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>

#include "timelinedb.h"
#include "timelinedb_util.h"

int main(int argc, char *argv[]) {
    (void)argc; // Unused parameter
    (void)argv; // Unused parameter
    int res= 0;
    RawTimelineValuesBuf buf;
    init_RawTimelineValuesBuf(&buf);
    buf.value_type = TR_analog_sint8;
    generate_sine_wave(&buf, 100, 1, 25.0f, 100.0f, 1000000);

    printf("Generated sine wave:\n");
    dump_RawTimelineValuesBuf(&buf);

    RawTimelineValuesBuf converted_buf;
    init_RawTimelineValuesBuf(&converted_buf);
    res= prepare_SampleRateConversion(&buf, 100000, &converted_buf);
    if (res != 0) {
        fprintf(stderr, "Failed to prepare sample rate conversion\n");
    } else {
        printf("Prepared for sample rate conversion:\n");
    }
    res= convert_sample_rate(&buf, &converted_buf);
    if (res != 0) {
        fprintf(stderr, "Failed to convert sample rate\n");
    }else {
        printf("Converted to 100kHz sample rate:\n");
        dump_RawTimelineValuesBuf(&converted_buf);
    }
    free_RawTimelineValuesBuf(&converted_buf);

    res= prepare_SampleRateConversion(&buf, 3000000, &converted_buf);
    if (res != 0) {
        fprintf(stderr, "Failed to prepare sample rate conversion\n");
    } else {
        printf("Prepared for sample rate conversion:\n");
    }
    res= convert_sample_rate(&buf, &converted_buf);
    if (res != 0) {
        fprintf(stderr, "Failed to convert sample rate\n");
    }else {
        printf("Converted to 3MHz sample rate:\n");
        dump_RawTimelineValuesBuf(&converted_buf);
    }
    free_RawTimelineValuesBuf(&converted_buf);

    RawTimelineValuesBuf neon_buf;
    init_RawTimelineValuesBuf(&neon_buf);
    res = prepare_NeonAlignedBuffer(&buf, &neon_buf);
    if (res != 0) {
        fprintf(stderr, "Failed to prepare Neon aligned buffer\n");
    } else {
        printf("Prepared Neon aligned buffer:\n");
    }
    convert_to_NeonAlignedBuffer(&buf, &neon_buf, 0, 0);
    dump_RawTimelineValuesBuf(&neon_buf);
    RawTimelineValuesBuf converted_neon_buf;
    init_RawTimelineValuesBuf(&converted_neon_buf);
    res = prepare_SampleRateConversion(&neon_buf, 300000, &converted_neon_buf);
    if (res != 0) {
        fprintf(stderr, "Failed to prepare sample rate conversion for Neon buffer\n");
    } else {
        printf("Prepared for sample rate conversion of Neon buffer:\n");
    }
    res = convert_sample_rate(&neon_buf, &converted_neon_buf);
    if (res != 0) {
        fprintf(stderr, "Failed to convert sample rate for Neon buffer\n");
    } else {
        printf("Converted Neon buffer:\n");
        dump_RawTimelineValuesBuf(&converted_neon_buf);
    }
    free_RawTimelineValuesBuf(&converted_neon_buf);
    free_RawTimelineValuesBuf(&neon_buf);

    // Performance test for SIMD conversion

    printf("Starting SIMD sample rate conversion performance test...\n");

    RawTimelineValuesBuf simd_input;
    init_RawTimelineValuesBuf(&simd_input);
    const uint32_t num_samples = 1000000; // large sample count
    alloc_RawTimelineValuesBuf(&simd_input, num_samples, 8, 16, 16, TR_SIMD_sint16x8);
    simd_input.time_exponent = -6;
    simd_input.time_step = 1;

    // Fill with pseudo-sine pattern for realism
    generate_sine_wave(&simd_input, num_samples, 8, 25.0f, 100.0f, 1500000);

    RawTimelineValuesBuf simd_output;
    init_RawTimelineValuesBuf(&simd_output);
    prepare_SampleRateConversion(&simd_input, 1200000, &simd_output); // pass1
    const char *bename="Unknown Backend";
    getBackendName(-1, &bename);
    struct timeval t0, t1;
    long elapsed_us = 0;

    gettimeofday(&t0, NULL);
    convert_sample_rate(&simd_input, &simd_output);
    gettimeofday(&t1, NULL);
    elapsed_us = (t1.tv_sec - t0.tv_sec) * 1000000L + (t1.tv_usec - t0.tv_usec);
    printf("%s sample rate conversion took %ld microseconds\n", bename, elapsed_us);

    setBackend(1); // Switch to SIMD backend
    getBackendName(-1, &bename);

    gettimeofday(&t0, NULL);
    convert_sample_rate(&simd_input, &simd_output);
    gettimeofday(&t1, NULL);
    elapsed_us = (t1.tv_sec - t0.tv_sec) * 1000000L + (t1.tv_usec - t0.tv_usec);
    printf("%s sample rate conversion took %ld microseconds\n", bename, elapsed_us);
    

    RawTimelineValuesBuf so_min, so_max;
    init_RawTimelineValuesBuf(&so_min);
    init_RawTimelineValuesBuf(&so_max);
    prepare_AggregationMinMax(&simd_input, &so_min, &so_max, 20);
    aggregate_MinMax(&simd_input, &so_min, &so_max, simd_input.nr_of_samples, 0);
    dump_RawTimelineValuesBuf(&so_min);
    dump_RawTimelineValuesBuf(&so_max);
    free_RawTimelineValuesBuf(&so_min);
    free_RawTimelineValuesBuf(&so_max);

    free_RawTimelineValuesBuf(&simd_input);
    free_RawTimelineValuesBuf(&simd_output);

    free_RawTimelineValuesBuf(&buf);
    
    return 0;
}