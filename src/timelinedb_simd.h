/*
    File: timelinedb_simd.h
    This file implements sample rate conversion and aggregation functions using SIMD (Single Instruction, Multiple Data) technology.
    Author: Barna Farago - MYND-Ideal kft.
    Date: 2025-07-01
    License: Modified MIT License. You can use it for learn, but I can sell it as closed source with some improvements...
*/
#ifndef TIMELINEDB_SIMD_H
#define TIMELINEDB_SIMD_H
#include <stdint.h>
#include "timelinedb.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    #define NEON_ENABLED
#elif defined(__AVX2__) || defined(__AVX__)
    #define AVX_ENABLED
#endif

typedef int (*fn_convert)(const RawTimelineValuesBuf *, RawTimelineValuesBuf *);
typedef int (*fn_aggregate_minmax)(const RawTimelineValuesBuf *, RawTimelineValuesBuf *, RawTimelineValuesBuf *, uint32_t, uint32_t, uint32_t);

typedef struct TimelineBackendFunctions {
    const char *name;
    fn_convert          convert_sample_rate_s16x8;
    fn_aggregate_minmax aggregate_minmax_s8;
    fn_aggregate_minmax aggregate_minmax_s16x8;
} TimelineBackendFunctions;

//Backend templates
extern const TimelineBackendFunctions gTimelineBackendFunctionsSIMD;
extern const TimelineBackendFunctions gTimelineBackendFunctionsC;

int init_InterpInfo(const RawTimelineValuesBuf *input, RawTimelineValuesBuf *output) ;
void free_InterpInfo(RawTimelineValuesBuf *output);

#endif // TIMELINEDB_SIMD_H