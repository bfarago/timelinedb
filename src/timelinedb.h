/*
    File: timelinedb.h
    This file implements the core functions for handling raw timeline values, including allocation, sample rate conversion, and aggregation.
    Author: Barna Farago - MYND-Ideal kft.
    Date: 2025-07-01
    License: Modified MIT License. You can use it for learn, but I can sell it as closed source with some improvements...
*/
#ifndef TIMELINEDB_H
#define TIMELINEDB_H

typedef struct {
    int id;
    char *name;
    char *description;
} TimelineEvent;

typedef struct {
    TimelineEvent *events;
    int count;
    int capacity;
} TimelineDB;

typedef enum {
    TR_undefined = 0,
    TR_digital1,
    TR_digital4,
    TR_digital8,
    TR_analog_sint8,
    TR_analog_float32,
    TR_analog_float64,
    TR_SIMD_sint16x8,
    TR_SIMD_sint24x8
} RawTimelineValueEnum;

typedef struct {
    uint32_t idx0;
    uint32_t idx1;
    uint16_t frac;
    uint16_t inv_frac;
} SampleInterpInfo;

typedef struct {
    double rate_ratio;
} SampleRateInfo;

/*
 Interlaved channel data is stored in a single buffer, where samples are stored in a linear sequence, and one sample may contains multiple channels.
*/
typedef struct {
    uint32_t buffer_size;
    uint32_t nr_of_samples;
    uint32_t time_step; // per sample in units given by time_exponent
    double   total_time_sec; // total duration covered by the raw samples
    int8_t   time_exponent;
    uint8_t  nr_of_channels;
    uint8_t  bitwidth;
    uint8_t  bytes_per_sample; // (7+ channel * bitwidth) / 8
    RawTimelineValueEnum value_type;
    unsigned char *valueBuffer;
    SampleRateInfo *sample_rate_info; // This is used for sample rate conversion
    SampleInterpInfo *prepared_data_src;
} RawTimelineValuesBuf;

uint8_t getBackendsCount();
int getBackendName(uint8_t index, const char **name);
int setBackend(uint8_t index);

void init_RawTimelineValuesBuf(RawTimelineValuesBuf *buf);
void alloc_RawTimelineValuesBuf(RawTimelineValuesBuf *buf,
    uint32_t nr_of_samples, uint8_t nr_of_channels, uint8_t bitwidth, uint8_t bytealignment, RawTimelineValueEnum value_type);
void free_RawTimelineValuesBuf(RawTimelineValuesBuf *buf);

void getEngineeringSampleRateFrequency(const RawTimelineValuesBuf *buf, double *freq_val, const char **freq_unit);
void getEngineeringTimeInterval(const RawTimelineValuesBuf *buf, double *time_val, const char **time_unit);

int getSampleValue_int8(const RawTimelineValuesBuf *buf, uint32_t sample_index, uint8_t channel, int8_t *value);
int getSampleValue_float32(const RawTimelineValuesBuf *buf, uint32_t sample_index, uint8_t channel, float *value);
int getSampleValue_SIMD_sint16x8(const RawTimelineValuesBuf *buf, uint32_t sample_index, uint8_t channel, int16_t *value);
int getSampleValue_SIMD_sint24x8(const RawTimelineValuesBuf *buf, uint32_t sample_index, uint8_t channel, int32_t *value);

int prepare_SampleRateConversion(const RawTimelineValuesBuf *input, uint32_t new_sample_rate_hz, RawTimelineValuesBuf *output);
int convert_sample_rate(const RawTimelineValuesBuf *input, RawTimelineValuesBuf *output);

int prepare_NeonAlignedBuffer(const RawTimelineValuesBuf *src, RawTimelineValuesBuf *dst);
int convert_to_NeonAlignedBuffer(const RawTimelineValuesBuf *src, RawTimelineValuesBuf *dst, uint8_t srcChannel, uint8_t dstChannel);
int convert_from_NeonAlignedBuffer(const RawTimelineValuesBuf *src, RawTimelineValuesBuf *dst);

int prepare_AggregationMinMax(const RawTimelineValuesBuf *input, RawTimelineValuesBuf *outMin, RawTimelineValuesBuf *outMax, uint32_t outSampleNr);
int aggregate_MinMax(const RawTimelineValuesBuf *input, RawTimelineValuesBuf *outMin, RawTimelineValuesBuf *outMax, uint32_t inSamples, uint32_t inOffset);

#endif
