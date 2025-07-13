# ALGORITHMS

## SAMPLE RATE CONVERSION

This module implements a **sample rate conversion (SRC)** system for multi-channel time series data, typically audio or signal traces with high temporal resolution. The converter supports both integer and fractional upsampling/downsampling.

### First Algorithm: Fixed-Point Linear Interpolation

The initial implementation uses a classic **linear interpolation** technique. Given an input array of samples, the algorithm computes output samples at positions that are evenly spaced in time, but may not align with the input sample indices.

For each output sample:

- The two nearest input samples (indices `idx0` and `idx1`) are determined.
- A fixed-point interpolation factor `frac_fixed` in Q16 format (0..65535) is calculated:
  
  ```c
  frac_fixed = ((uint64_t)accum << 16) / scale;
  ```

This implementation performs one 64-bit division per output sample, which can be a performance bottleneck in high-throughput scenarios.

### Second Algorithm: Bresenham-style Fractional Accumulation

The second implementation adopts a method inspired by **Bresenham's line algorithm**, originally developed for rasterizing lines on discrete grids. In the context of sample rate conversion, this technique replaces division-heavy calculations with efficient integer arithmetic by leveraging a **fixed-point fractional accumulator**.

Fundamentally, the problem of resampling can be modeled as mapping evenly spaced samples in the output domain to non-integer positions in the input domain. If the ratio between input and output sample rates is not an integer, each output sample "steps" forward in input space by a fractional amount.

Instead of computing the precise floating-point position for each output sample (and then interpolating), this method keeps track of a fixed-point "accumulator" that represents the sub-sample phase error. The accumulator is incremented by a fixed step each iteration:

```c
frac += step & 0xFFFF;
index += step >> 16;
if (frac >= 0x10000) {
    ++index;
    frac -= 0x10000;
}
```

Here, `step` is a 32-bit fixed-point number where the upper 16 bits represent the integer advancement in input space, and the lower 16 bits represent the fractional part. The algorithm maintains a running index `index` into the input sample buffer and a residual fraction `frac` that is corrected when it exceeds 1.0 in Q16 format (`0x10000`).

This process is analogous to Bresenham's error accumulation when drawing a line: instead of computing each pixel coordinate via slope, the algorithm accumulates error terms and steps to the next discrete sample when the accumulated error exceeds a threshold. This not only eliminates division operations but also ensures consistent step behavior with minimal rounding error over long sequences.

The resulting interpolation is still linear, using two neighboring input samples. However, due to the integer-only loop body, this method is highly suitable for SIMD acceleration (e.g., ARM NEON or Intel SSE/AVX), where branching and division are costly but vectorized addition and shifting are extremely fast.

## Decimation

When higher sample-rate input data shall be converted to a lower frequency samples to reduce the memory needed to store the information, often some decimation algorithms are used. Due to there is as future goal, we will implement some FIR filter later. Right now the project is focusing on visualization first.

### Downsampling for Visualization

A third algorithm is implemented to support real-time visualization of large signal arrays, such as waveform display on a screen with limited pixel resolution.

Instead of performing classical decimation (which would reduce data size by selecting every N-th sample, possibly introducing aliasing), this method computes **per-pixel aggregates** using the minimum and maximum values of the signal within a fixed horizontal window.

Each pixel column on the screen corresponds to a horizontal time range (i.e., a block of input samples). For each column:

- The algorithm scans the corresponding input sample range.
- It records the **minimum** and **maximum** sample value within this window.
- A vertical line is rendered from the min to the max value, creating a filled, shaded stroke that preserves amplitude variation.

This approach preserves critical waveform details (like peaks and troughs) even under extreme downsampling ratios. It's particularly effective for oscilloscopic or audio waveform-style visualizations.

The algorithm is extremely efficient, as it:
- Avoids interpolation and division entirely.
- Works well with SIMD acceleration for min/max reduction across blocks.
- Supports real-time interaction (scrolling, zooming) with minimal latency.

Unlike true downsampling for signal processing, this method is designed purely for **graphical representation**, and does not alter the underlying data fidelity or sample rate.
