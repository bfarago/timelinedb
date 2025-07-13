# TIMELINEDB V0.1

**TimelineDB** is a low-level C library designed to store and process time-domain data for multi-layered software systems, with an emphasis on embedded use cases, real-time visualization, and SIMD-optimized performance.

 👉 See [Algorithms Explained](doc/algorithms.md) for detailed descriptions of the signal processing algorithms used in TimelineDB.

## Overview

The library offers:
- Efficient memory layout for high-throughput time series data
- Support for multiple channels (interleaved format)
- Linear sample rate conversion and aggregation (min/max downsampling)
- SIMD-accelerated operations (NEON / SSE planned)
- Simple file-less and GUI integration (e.g. SDL-based waveform display)

It is written entirely in C to ensure maximum portability — including platforms with limited toolchain support (e.g. FPGA soft-CPUs, or bare-metal ARM targets).

## Rationale

While modern CPUs offer powerful SIMD instructions, C compilers don't always optimize memory access patterns well. TimelineDB intentionally uses interleaved memory formats to match vector register widths and enable direct, aligned loads for multi-channel data.

This design facilitates operations like:
- Sample rate conversion using vectorized linear interpolation
- Min/max aggregation for zoomed-out waveform rendering
- Efficient data traversal on embedded ARM or Apple M-series CPUs

## Features

- RAM-based timeline buffers with:
  - Timebase definition (step + exponent)
  - Interleaved per-sample layout
  - Optional SIMD-ready value types (`int16x8`, `int8x16`, etc.)
- Conversion functions:
  - `convert_sample_rate_*` — SIMD & scalar versions
  - `convert_downsample_minmax_*` — aggregate for display
- Visualization:
  - Reference SDL2-based frontend with waveform drawing
  - Scalable design up to 32 channels
  - Color-coded, labeled curves with support for font rendering (SDL_ttf)
- Signal generation (for test/demo):
  - Per-channel configurable sine series
  - Randomized frequency, amplitude, harmonics
  - Generates continuous signal based on current timestamp

## Build Instructions

Dependencies (via Homebrew on macOS):

```bash
brew install sdl2 sdl2_ttf
```

Build:

```bash
make devtest     # console test tool
make devgui      # SDL-based waveform viewer
```

## Architecture

```text
+---------------------------+
|   RawTimelineValuesBuf    | -- stores metadata + raw buffer
+---------------------------+
        |
        v
+---------------------------+
|   convert_sample_rate_*   |
|   convert_downsample_*    |
+---------------------------+
        |
        v
+---------------------------+
|     SDL2 Visualization    |
+---------------------------+
```

## Status

This is an early prototype. API changes, refactoring, and structural cleanups are ongoing. Contributions and feedback are welcome.

## License

MvMIT (c) 2025 Barna Faragó