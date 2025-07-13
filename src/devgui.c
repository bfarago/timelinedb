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

int g_screen_w = 800;
int g_screen_h = 600;
int g_screen_size_changed = 1;

typedef struct {
    float amplitude;
    float frequency;
    float phase;
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



RawTimelineValuesBuf g_timeline_bufs[MAX_TIMELINE_BUFS];
RawTimelineValuesBuf g_timeline_min[MAX_TIMELINE_BUFS];
RawTimelineValuesBuf g_timeline_max[MAX_TIMELINE_BUFS];
TimelineDB g_timeline_db;
TimelineEvent g_timeline_events[MAX_TIMELINE_CHANNELS];


void db_init() {
    for (int i = 0; i < MAX_TIMELINE_CHANNELS; i++) {
        g_signal_params[i].amplitude = 10000.0f + (rand() % 20000); // 10000 - 29999
        g_signal_params[i].frequency = 0.1f + ((float)(rand() % 1000) / 10.0f); // 0.1 - 10.0 Hz
        g_signal_params[i].phase = ((float)(rand() % 6283)) / 1000.0f; // 0 - ~2π
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

void db_update(Uint32 timestamp) {
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
void SDL_DrawText(SDL_Renderer* renderer, const char* text, int x, int y) {
    static TTF_Font* font = NULL;
    if (!font) {
        font = TTF_OpenFont("/System/Library/Fonts/Supplemental/Arial.ttf", 12);
        if (!font) {
            SDL_Log("TTF_OpenFont failed: %s", TTF_GetError());
            return;
        }
    }

    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* surface = TTF_RenderText_Solid(font, text, white);
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
    for (uint32_t i = 0; i < nr_of_samples - 1; i++) {
        int16_t v1, v2;
        getSampleValue_SIMD_sint16x8(max_buf, i, curve->channelidx, &v1);
        getSampleValue_SIMD_sint16x8(min_buf, i, curve->channelidx, &v2);
        SDL_RenderDrawLine(renderer,
            start_x + i * drawable_width / nr_of_samples,
            curve->offsety - (int)(v1 * curve->scale),
            start_x + (i + 1) * drawable_width / nr_of_samples,
            curve->offsety - (int)(v2 * curve->scale));
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
    SDL_Window* window = SDL_CreateWindow("Draw curve",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    SDL_GetWindowSize(window, &g_screen_w, &g_screen_h);
    Uint32 now = SDL_GetTicks();
    setBackend(1); // Use SIMD backend
    db_init();
    db_update(now);
    screen_update(now, renderer);

    int running = 1;
    Uint32 last_timer = SDL_GetTicks();
    SDL_Event event;

    while (running) {

        SDL_Delay(10);
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

        if (now - last_timer >= 20) {
            on_timer_tick(now, renderer);
            last_timer = now;
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    db_free();
    return 0;
}
