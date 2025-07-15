/*
    File: devgui.c
    This file implements a developer GUI for testing and debugging the timeline database functions.
    Author: Barna Farago - MYND-Ideal kft.
    Date: 2025-07-01
    License: Modified MIT License. You can use it for learn, but I can sell it as closed source with some improvements...
*/
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdint.h>
#include "timelinedb.h"
#include "timelinedb_util.h"
#include <math.h>
#include <stdlib.h>

#define MAX_TIMELINE_CHANNELS 32
#define MAX_TIMELINE_BUFS 4
#define MAX_TIMELINE_SAMPLES 1000000
#define DELAY_SCREEN_REFRESH (1000 / 30 )

#define LUT_SIZE 1024
#define LUT_MASK (LUT_SIZE - 1)

int g_screen_w = 800;
int g_screen_h = 600;
int g_screen_size_changed = 1;

TTF_Font *g_font_label = NULL;
TTF_Font *g_font_axis = NULL;

typedef struct {
//    float s, c;         // aktuális sin és cos
    float amplitude;
    float w;
    float wdt;
    float sin_wdt;
    float cos_wdt;
    RawTimelineValuesBuf* buf;
} SignalGenState;

typedef struct {
    float amplitude;
    float frequency;
    float phase;
    SignalGenState st;
} SignalParams;

typedef struct {
    int id;
    TimelineEvent *event;
    uint8_t channelidx;
    RawTimelineValuesBuf *buf;
    RawTimelineValuesBuf *min_buf;
    RawTimelineValuesBuf *max_buf;
    int16_t offsety;
    int16_t height;
    double scale;
    int color;
} SignalCurve;

typedef struct {
    int count;
    int middle_offset; // there is the common offset for all curves
    int start_y;
    int height;
    int label_width;
    int right_margin;
} SignalCurvesView;

SignalCurve g_signal_curves[MAX_TIMELINE_CHANNELS];
SignalCurvesView g_signal_curves_view = {
    .count = MAX_TIMELINE_CHANNELS,
    .middle_offset = 0,
    .start_y = 100,
    .height = 400, // Default height, can be adjusted later
    .label_width = 100, // Width for channel labels
    .right_margin = 50 // Right margin for scrollbar or other UI elements
};
static SignalParams g_signal_params[MAX_TIMELINE_CHANNELS];
const float g_sample_rate = 1000000.0f;
float g_dt = 1.0f / 1000000.0f;;


RawTimelineValuesBuf g_timeline_bufs[MAX_TIMELINE_BUFS];
RawTimelineValuesBuf g_timeline_min[MAX_TIMELINE_BUFS];
RawTimelineValuesBuf g_timeline_max[MAX_TIMELINE_BUFS];
TimelineDB g_timeline_db;
TimelineEvent g_timeline_events[MAX_TIMELINE_CHANNELS];

float sin_lut[LUT_SIZE];
float cos_lut[LUT_SIZE];

void db_init() {
    for (int i = 0; i < LUT_SIZE; i++) {
        sin_lut[i] = sinf(2.0f * M_PI * (float)i / LUT_SIZE);
        cos_lut[i] = cosf(2.0f * M_PI * (float)i / LUT_SIZE);
    }
    for (int i = 0; i < MAX_TIMELINE_CHANNELS; i++) {
        SignalParams* p = &g_signal_params[i];
        p->amplitude = 10000.0f + (rand() % 20000); // 10000 - 29999
        p->frequency = 0.1f + ((float)(rand() % 10000) / 10.0f); // 0.1 - 10.0 Hz
        p->phase = ((float)(rand() % 6283)) / 1000.0f; // 0 - ~2π
        
        SignalGenState *sgs = &p->st;
        sgs->buf = &g_timeline_bufs[ i >> 3 ];
        float f = p->frequency;
        float w = 2.0f * M_PI * f;
        sgs->w = w;
        float wdt = w * g_dt;
        sgs->wdt = wdt;
        sgs->sin_wdt = sinf(wdt);
        sgs->cos_wdt = cosf(wdt); 
    }
    g_signal_curves_view.start_y = 100;
    g_signal_curves_view.label_width = 100; // Width for channel labels
    g_signal_curves_view.right_margin = 50; // Right margin for scrollbar or other UI elements

    for (int i = 0; i < MAX_TIMELINE_CHANNELS; i++) {
        g_timeline_events[i].id = i;
        static char name_buf[64];
        static char desc_buf[128];
        sprintf(name_buf, "signal%03d", i + 1);
        sprintf(desc_buf, "Auto-generated signal %d", i + 1);
        g_timeline_events[i].name = strdup(name_buf);
        g_timeline_events[i].description = strdup(desc_buf);
        g_signal_curves[i].event = &g_timeline_events[i];
        uint8_t channelidx = i % 8; // 8 channels per buffer
        uint8_t buffidx = i / 8; // 4 buffers
        g_signal_curves[i].id = i;
        g_signal_curves[i].channelidx = channelidx;
        g_signal_curves[i].offsety = 0;
        g_signal_curves[i].scale = 1.0;
        // Use a more distinct color palette, assuming black background
        const int color_table[32] = {
            0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00,
            0xFF00FF, 0x00FFFF, 0xFFA500, 0x8A2BE2,
            0x7FFF00, 0xDC143C, 0x00CED1, 0xFF1493,
            0xFFD700, 0x4B0082, 0xADFF2F, 0x00FA9A,
            0xFF6347, 0x40E0D0, 0xEE82EE, 0x9ACD32,
            0x20B2AA, 0xFF4500, 0xDA70D6, 0x1E90FF,
            0xFF69B4, 0x8B0000, 0x2E8B57, 0x9932CC,
            0xB22222, 0x5F9EA0, 0xF08080, 0x008080
        };
        g_signal_curves[i].color = color_table[i % 32];
        g_signal_curves[i].event = &g_timeline_events[i];
        g_signal_curves[i].buf = &g_timeline_bufs[buffidx];
        g_signal_curves[i].min_buf = &g_timeline_min[buffidx];
        g_signal_curves[i].max_buf = &g_timeline_max[buffidx];
        g_signal_curves[i].height = 0; // Will be set later based on screen height
    }
    g_timeline_db.events = g_timeline_events;
    g_timeline_db.count = MAX_TIMELINE_CHANNELS;
    
    for (int i = 0; i < MAX_TIMELINE_BUFS; i++) {
        init_RawTimelineValuesBuf(&g_timeline_bufs[i]);
        init_RawTimelineValuesBuf(&g_timeline_min[i]);
        init_RawTimelineValuesBuf(&g_timeline_max[i]);
        alloc_RawTimelineValuesBuf(&g_timeline_bufs[i], MAX_TIMELINE_SAMPLES, 8, 16, 16, TR_SIMD_sint16x8);
        alloc_RawTimelineValuesBuf(&g_timeline_min[i], g_screen_w, 8, 16, 16, TR_SIMD_sint16x8);
        alloc_RawTimelineValuesBuf(&g_timeline_max[i], g_screen_w, 8, 16, 16, TR_SIMD_sint16x8);
    }
}
void db_free() {
    for (int i = 0; i < MAX_TIMELINE_BUFS; i++) {
        free_RawTimelineValuesBuf(&g_timeline_bufs[i]);
    }
    for (int i = 0; i < MAX_TIMELINE_CHANNELS; i++) {
        free(g_timeline_events[i].name);
        free(g_timeline_events[i].description);
    }
}

/*
TODO : 
1) 32x4 sinf cosf can be converted to LUT 
2) Move to lib simmd.c

Pure C version runtime was: 220ms,
Rodrigues and avx implementation: 14ms
@1M sample x 32 channel  
*/
// Apple NEON SIMD implementation using Rodrigues formula
#if (defined(__ARM_NEON) || defined(__ARM_NEON__))
#include <arm_neon.h>
void db_update(Uint32 timestamp){
    const float t0 = (float)timestamp / 1000.0f;
    for (int b = 0; b < 4; b++) {
        RawTimelineValuesBuf* buf = &g_timeline_bufs[b];
        int16_t* data = (int16_t*)buf->valueBuffer;
        uint32_t samples = buf->nr_of_samples;

        float amp_f[8], s_f[8], c_f[8], sin_wdt_f[8], cos_wdt_f[8];
        for (int ch = 0; ch < 8; ++ch) {
            int global_ch = b * 8 + ch;
            SignalParams* p = &g_signal_params[global_ch];

            amp_f[ch] = p->amplitude;
            float w = 2.0f * M_PI * p->frequency;
            float angle = w * t0 + p->phase;
            s_f[ch] = sinf(angle);
            c_f[ch] = cosf(angle);
            float wdt = w * g_dt;
            sin_wdt_f[ch] = sinf(wdt);
            cos_wdt_f[ch] = cosf(wdt);
        }

        float32x4_t amp0 = vld1q_f32(&amp_f[0]);
        float32x4_t amp1 = vld1q_f32(&amp_f[4]);
        float32x4_t s0 = vld1q_f32(&s_f[0]);
        float32x4_t s1 = vld1q_f32(&s_f[4]);
        float32x4_t c0 = vld1q_f32(&c_f[0]);
        float32x4_t c1 = vld1q_f32(&c_f[4]);
        float32x4_t sinwdt0 = vld1q_f32(&sin_wdt_f[0]);
        float32x4_t sinwdt1 = vld1q_f32(&sin_wdt_f[4]);
        float32x4_t coswdt0 = vld1q_f32(&cos_wdt_f[0]);
        float32x4_t coswdt1 = vld1q_f32(&cos_wdt_f[4]);

        for (uint32_t s_idx = 0; s_idx < samples; s_idx++) {
            float32x4_t v0 = vmulq_f32(amp0, s0);
            float32x4_t v1 = vmulq_f32(amp1, s1);

            int32x4_t i32_0 = vcvtq_s32_f32(v0);
            int32x4_t i32_1 = vcvtq_s32_f32(v1);

            int16x4_t i16_0 = vqmovn_s32(i32_0);
            int16x4_t i16_1 = vqmovn_s32(i32_1);
            int16x8_t packed = vcombine_s16(i16_0, i16_1);
            vst1q_s16(&data[s_idx * 8], packed);

            float32x4_t s0_new = vmlaq_f32(vmulq_f32(s0, coswdt0), c0, sinwdt0);
            float32x4_t s1_new = vmlaq_f32(vmulq_f32(s1, coswdt1), c1, sinwdt1);
            float32x4_t c0_new = vmlsq_f32(vmulq_f32(c0, coswdt0), s0, sinwdt0);
            float32x4_t c1_new = vmlsq_f32(vmulq_f32(c1, coswdt1), s1, sinwdt1);

            s0 = s0_new;
            s1 = s1_new;
            c0 = c0_new;
            c1 = c1_new;
        }
    }
}
#else
void db_update(Uint32 timestamp){
    const float t0 = (float)timestamp / 1000.0f;
    for (int b = 0; b < 4; b++) {
        RawTimelineValuesBuf* buf = &g_timeline_bufs[b];
        int16_t* data = (int16_t*)buf->valueBuffer;
        uint32_t samples = buf->nr_of_samples;
//        const uint32_t channels = 8; // fixen 8 csatorna / buffer

        // 1. előkészítjük a 8 csatornához az amplitúdót, sin/cos induló értéket stb.
        float amp_f[8], s_f[8], c_f[8], sin_wdt_f[8], cos_wdt_f[8];
        for (int ch = 0; ch < 8; ++ch) {
            int global_ch = b * 8 + ch;
            SignalParams* p = &g_signal_params[global_ch];

            amp_f[ch] = p->amplitude;
            float w = 2.0f * M_PI * p->frequency;
            float angle = w * t0 + p->phase;
            s_f[ch] = sinf(angle);
            c_f[ch] = cosf(angle);
            float wdt = w * g_dt;
            sin_wdt_f[ch] = sinf(wdt);
            cos_wdt_f[ch] = cosf(wdt);
        }

        // 2. betöltjük SIMD regiszterekbe
        __m256 amp      = _mm256_loadu_ps(amp_f);
        __m256 s        = _mm256_loadu_ps(s_f);
        __m256 c        = _mm256_loadu_ps(c_f);
        __m256 sin_wdt  = _mm256_loadu_ps(sin_wdt_f);
        __m256 cos_wdt  = _mm256_loadu_ps(cos_wdt_f);

        // 3. végigmegyünk a mintákon
        for (uint32_t s_idx = 0; s_idx < samples; s_idx++) {
            __m256 val = _mm256_mul_ps(amp, s);
            __m256i i32 = _mm256_cvtps_epi32(val);
            __m128i lo = _mm256_extractf128_si256(i32, 0);
            __m128i hi = _mm256_extractf128_si256(i32, 1);
            __m128i i16 = _mm_packs_epi32(lo, hi);
            _mm_storeu_si128((__m128i*)&data[s_idx * 8], i16);

            // Rodrigues-frissítés
            __m256 s_new = _mm256_add_ps(
                _mm256_mul_ps(s, cos_wdt),
                _mm256_mul_ps(c, sin_wdt)
            );
            __m256 c_new = _mm256_sub_ps(
                _mm256_mul_ps(c, cos_wdt),
                _mm256_mul_ps(s, sin_wdt)
            );
            s = s_new;
            c = c_new;
        }
    }
}
#endif

#if 0
void db_update3(Uint32 timestamp) {
    const float t0 = (float)timestamp / 1000.0f;
    for(uint8_t gch = 0; gch < MAX_TIMELINE_CHANNELS; gch++){
        SignalParams* p = &g_signal_params[gch];
        SignalGenState *sgs = &p->st;
        RawTimelineValuesBuf* buf = sgs->buf;
        int16_t* data = (int16_t*)buf->valueBuffer;

        float phase = p->phase;
        float amp = p->amplitude;
        float w = sgs->w;
        uint32_t samples = buf->nr_of_samples;
        uint32_t channels = 8;
        uint8_t ch = gch & 7;
        float sin_wdt = sgs->sin_wdt;
        float cos_wdt = sgs->cos_wdt;
        
        float angle = w * t0 + phase;
        float phase_frac = angle / (2.0f * M_PI); 
        phase_frac -= (int)phase_frac;
        if (phase_frac < 0) phase_frac += 1.0f;
        uint32_t idx = (uint32_t)(phase_frac * LUT_SIZE) & LUT_MASK;
        float s = sin_lut[idx];
        float c = cos_lut[idx];
        
        for (uint32_t s_idx = 0; s_idx < samples; s_idx++) {
            int16_t sample = (int16_t)(amp * s);
            data[s_idx * channels + ch] = sample;
            float s_new = s * cos_wdt + c * sin_wdt;
            float c_new = c * cos_wdt - s * sin_wdt;
            s = s_new;
            c = c_new;
        }
    }
}   


void db_update1(Uint32 timestamp) {
    const float sample_rate = 1000000.0f;
    const float t0 = (float)timestamp / 1000.0f;

    for (int b = 0; b < MAX_TIMELINE_BUFS; b++) {
        RawTimelineValuesBuf* buf = &g_timeline_bufs[b];
        int16_t* data = (int16_t*)buf->valueBuffer;
        uint32_t samples = buf->nr_of_samples;
        uint32_t channels = buf->nr_of_channels;

        for (uint32_t s = 0; s < samples; s++) {
            float t = t0 + (float)s / sample_rate;
            for (uint32_t ch = 0; ch < channels; ch++) {
                int global_ch = b * 8 + ch;
                SignalParams* p = &g_signal_params[global_ch];
                float value = p->amplitude * sinf(2.0f * M_PI * p->frequency * t + p->phase);
                int16_t sample = (int16_t)value;
                data[s * channels + ch] = sample;
            }
        }
    }
}
void db_update2(Uint32 timestamp) {
    const float sample_rate = 1000000.0f;
    const float t0 = (float)timestamp / 1000.0f;
    // Δt = 1/sample_rate
    float dt = 1.0f / sample_rate;
    for (int b = 0; b < MAX_TIMELINE_BUFS; b++) {
        RawTimelineValuesBuf* buf = &g_timeline_bufs[b];
        int16_t* data = (int16_t*)buf->valueBuffer;
        uint32_t samples = buf->nr_of_samples;
        uint32_t channels = buf->nr_of_channels;

        for (uint32_t ch = 0; ch < channels; ch++) {
            int global_ch = b * 8 + ch;
            SignalParams* p = &g_signal_params[global_ch];

            float f = p->frequency;
            float w = 2.0f * M_PI * f;
            float phase = p->phase;
            float amp = p->amplitude;
            float wdt = w * dt;

            // rekurzív sin hullámgenerátor (Rodrigues-formula)
            float sin_wdt = sinf(wdt);
            float cos_wdt = cosf(wdt);
            float s = sinf(w * t0 + phase);
            float c = cosf(w * t0 + phase);

            for (uint32_t s_idx = 0; s_idx < samples; s_idx++) {
                int16_t sample = (int16_t)(amp * s);
                data[s_idx * channels + ch] = sample;

                // rekurzív következő sin/cos érték
                float s_new = s * cos_wdt + c * sin_wdt;
                float c_new = c * cos_wdt - s * sin_wdt;
                s = s_new;
                c = c_new;
            }
        }
    }
} 
#endif
void init_fonts() {
    #ifdef APPLE
    g_font_label = TTF_OpenFont("/System/Library/Fonts/Supplemental/Arial.ttf", 12);
    g_font_axis  = TTF_OpenFont("/System/Library/Fonts/Supplemental/Arial.ttf", 9);
    #else
    g_font_label = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",12);
    g_font_axis  = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",9);
    #endif
    if (!g_font_label) {
        SDL_Log("TTF_OpenFont failed: %s", TTF_GetError());
        return;
    }
}

void SDL_DrawText(SDL_Renderer* renderer, const char* text, int x, int y) {
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* surface = TTF_RenderText_Solid(g_font_label, text, white);
    if (!surface) {
        SDL_Log("TTF_RenderText_Solid failed: %s", TTF_GetError());
        return;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        SDL_Log("SDL_CreateTextureFromSurface failed: %s", SDL_GetError());
        SDL_FreeSurface(surface);
        return;
    }

    SDL_Rect dst = {x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, NULL, &dst);

    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}
void draw_one_curve(SDL_Renderer* renderer, const SignalCurve* curve) {
    if (!curve || !curve->buf || !curve->min_buf || !curve->max_buf) return;

    RawTimelineValuesBuf* min_buf = curve->min_buf;
    RawTimelineValuesBuf* max_buf = curve->max_buf;
    uint32_t nr_of_samples = min_buf->nr_of_samples;
    if (nr_of_samples == 0) return;

    SDL_SetRenderDrawColor(renderer, (curve->color >> 16) & 0xFF, (curve->color >> 8) & 0xFF, curve->color & 0xFF, 255);

    int start_x = g_signal_curves_view.label_width;

    // Draw a horizontal separator line between signal areas
    SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
    SDL_RenderDrawLine(renderer,
        0,
        curve->offsety + curve->height,
        g_screen_w,
        curve->offsety + curve->height);

    // Restore curve color
    SDL_SetRenderDrawColor(renderer, (curve->color >> 16) & 0xFF, (curve->color >> 8) & 0xFF, curve->color & 0xFF, 255);

    // Draw signal name (if using SDL_ttf, add actual text rendering here)
    SDL_DrawText(renderer, curve->event->name, 0, curve->offsety);

    int drawable_width = g_screen_w - g_signal_curves_view.label_width - g_signal_curves_view.right_margin;
    uint16_t *minp= (uint16_t *)min_buf->valueBuffer;
    uint16_t *maxp= (uint16_t *)max_buf->valueBuffer;
    minp+=curve->channelidx;
    maxp+=curve->channelidx;
    uint8_t d = min_buf->nr_of_channels;
    for (uint32_t i = 0; i < nr_of_samples - 1; i++) {
        int16_t v1, v2;
        //getSampleValue_SIMD_sint16x8(max_buf, i, curve->channelidx, &v1);
        //getSampleValue_SIMD_sint16x8(min_buf, i, curve->channelidx, &v2);
        v1 = *minp;
        v2 = *maxp;
        minp+= d; maxp+= d;
        SDL_RenderDrawLine(renderer,
            start_x + i * drawable_width / nr_of_samples,
            curve->offsety - (int)(v1 * curve->scale),
            start_x + (i + 1) * drawable_width / nr_of_samples,
            curve->offsety - (int)(v2 * curve->scale));
    }
}
void draw_time_label(SDL_Renderer *renderer, int x, int t_ms) {
    char label[16];
    snprintf(label, sizeof(label), "%d ms", t_ms);

    SDL_Color textColor = {255, 255, 255, 255};
    SDL_Surface *textSurface = TTF_RenderText_Blended(g_font_axis, label, textColor);
    if (!textSurface) return;

    SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
    if (!textTexture) {
        SDL_FreeSurface(textSurface);
        return;
    }

    int text_w = textSurface->w;
    int text_h = textSurface->h;
    SDL_Rect dstRect = { x - text_w / 2, 50, text_w, text_h };

    SDL_RenderCopy(renderer, textTexture, NULL, &dstRect);
    SDL_DestroyTexture(textTexture);
    SDL_FreeSurface(textSurface);
}
void draw_time_axis(SDL_Renderer *renderer, Uint32 timestamp) {
(void)timestamp; // Unused in this example
    const int top = 50; // Top position for the time axis
    const int axis_height = g_signal_curves_view.start_y - top;
    const int time_range_ms = 1000;
    const int tick_interval_ms = 10;       // fine
    const int middle_interval_ms = 50;       // middle
    const int major_tick_interval_ms = 100; // coarse
    const int label_width = g_signal_curves_view.label_width;
    const int right_margin = g_signal_curves_view.right_margin;
    const int plot_area_w = g_screen_w - label_width - right_margin;
    const float pixels_per_ms = (float)plot_area_w / time_range_ms;
    const int axisline_height = g_screen_h - top - 50; // 50px for bottom margin

    // Reset timestamp to 0 for axis drawing due to the first pixel column is always 0 (or user choose it by scrolling)
    timestamp = 0;
    // Vonalak és szöveg színe
    SDL_SetRenderDrawColor(renderer, 16, 16, 16, 255); // halványabb szürke
    SDL_Rect axis_rect = {0, top, g_screen_w, axis_height};
    SDL_RenderFillRect(renderer, &axis_rect);

    for (int t = 0; t <= time_range_ms; t += tick_interval_ms) {
        int x = label_width + (int)(t * pixels_per_ms);
        if (x >= g_screen_w - right_margin) break;

        if (t % major_tick_interval_ms == 0) {
            SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255);
            draw_time_label(renderer, x, t);
        } else if (t % middle_interval_ms == 0) {
            SDL_SetRenderDrawColor(renderer, 64, 64, 64, 255);
        } else if (t % tick_interval_ms == 0) {
            SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
        } else {
            continue; // Skip drawing for non-tick intervals
        }
        SDL_RenderDrawLine(renderer, x, top, x, axisline_height);
    }
}
void draw_curves(SDL_Renderer* renderer) {
    for (int i = 0; i < MAX_TIMELINE_CHANNELS; i++) {
        if (g_signal_curves[i].buf && g_signal_curves[i].buf->valueBuffer) {
            draw_one_curve(renderer, &g_signal_curves[i]);
        }
    }
}
void screen_update(Uint32 timestamp, SDL_Renderer* renderer) {
    (void)timestamp; // Unused in this example
    if (g_screen_size_changed) {
        g_screen_size_changed = 0;
        for (int i = 0; i < MAX_TIMELINE_BUFS; i++) {
            free_RawTimelineValuesBuf(&g_timeline_min[i]);
            free_RawTimelineValuesBuf(&g_timeline_max[i]);
            alloc_RawTimelineValuesBuf(&g_timeline_min[i], g_screen_w, 8, 16, 16, TR_SIMD_sint16x8);
            alloc_RawTimelineValuesBuf(&g_timeline_max[i], g_screen_w, 8, 16, 16, TR_SIMD_sint16x8);
            prepare_AggregationMinMax(&g_timeline_bufs[i], &g_timeline_min[0], &g_timeline_max[0], g_screen_w);
        }
        g_signal_curves_view.middle_offset = g_screen_h / 2;
        g_signal_curves_view.height = g_screen_h - g_signal_curves_view.start_y - 50; // 50px for bottom margin
        for (int i = 0; i < MAX_TIMELINE_CHANNELS; i++) {
            g_signal_curves[i].height = g_signal_curves_view.height / MAX_TIMELINE_CHANNELS;
            g_signal_curves[i].offsety = g_signal_curves_view.start_y + i * g_signal_curves[i].height;
            g_signal_curves[i].scale = (float)g_signal_curves[i].height / 65536.0f; // Scale to fit in height
        }     
    }
    for (int i = 0; i < MAX_TIMELINE_BUFS; i++) {
        aggregate_MinMax(&g_timeline_bufs[i], &g_timeline_min[i], &g_timeline_max[i]);
    }
    if (!renderer) {
        SDL_Log("Failed to get renderer");
        return;
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // Set background color to black
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); // Set color for min/max lines
    draw_time_axis(renderer, timestamp);
    draw_curves(renderer);
    SDL_RenderPresent(renderer);
}
void on_timer_tick(Uint32 timestamp, SDL_Renderer* renderer) {
    db_update(timestamp);
    screen_update(timestamp, renderer);
}

int main() {
    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();
    init_fonts();
    SDL_Window* window = SDL_CreateWindow("Draw curve",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, g_screen_w, g_screen_h, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED| SDL_RENDERER_PRESENTVSYNC);

    SDL_GetWindowSize(window, &g_screen_w, &g_screen_h);
    Uint32 now = SDL_GetTicks();
    setBackend(1); // Use SIMD backend
    const char * bename= NULL;
    getBackendName(-1, &bename);
    printf("%s\n", bename);
    db_init();
    db_update(now);
    screen_update(now, renderer);

    int running = 1;
    Uint32 last_timer = SDL_GetTicks();
    SDL_Event event;

    while (running) {

        // SDL_Delay(DELAY_SCREEN_REFRESH);
        now = SDL_GetTicks();

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT)
                running = 0;
            if (event.type == SDL_MOUSEMOTION) {
            }
            if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    g_screen_w = event.window.data1;
                    g_screen_h = event.window.data2;
                    g_screen_size_changed = 1;
                    screen_update(now, renderer);
                }
            }
        }
        int32_t elapsed = now - last_timer;
        // if (elapsed < 0) elapsed =0;
        if ( elapsed >= DELAY_SCREEN_REFRESH) {
            on_timer_tick(now, renderer);
            last_timer = now;
        }else{
            int32_t next=DELAY_SCREEN_REFRESH - elapsed;
            if (next>0) SDL_Delay(next);
        }

    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    db_free();
    return 0;
}
