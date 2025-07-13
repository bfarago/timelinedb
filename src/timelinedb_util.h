/*
    File: timelinedb_util.h
    This file contains some util functions like a hex dump and sine wave generation for testing.
    Author: Barna Farago - MYND-Ideal kft.
    Date: 2025-07-01
    License: Modified MIT License. You can use it for learn, but I can sell it as closed source with some improvements...
*/
#ifndef TIMELINEDB_UTIL_H
#define TIMELINEDB_UTIL_H
#include <stdint.h>
#include "timelinedb.h"

void dump_RawTimelineValuesBuf(const RawTimelineValuesBuf *buf);
void generate_sine_wave(RawTimelineValuesBuf *buf, uint32_t num_samples, uint8_t num_channels, float period, float amplitude, uint32_t sample_rate_hz);

#endif // TIMELINEDB_UTIL_H