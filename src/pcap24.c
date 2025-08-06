/*
    File: pcap24.c
    This file implements a developer GUI for testing and debugging the timeline database functions.
    Author: Barna Farago - MYND-Ideal kft.
    Date: 2025-07-01
    License: Modified MIT License. You can use it for learn, but I can sell it as closed source with some improvements...
    Description: This file contains the main loop, event handling, and rendering functions for the timeline database GUI.
    The main goal is to provide a visual representation of a pcap (Wireshark) stream as athe timeline data, allowing for
    easy debugging and testing of the timeline database functions. Based on this use-case, we could probably improve the
    timeline database functions to be more efficient and easier to use, while we can also have a tool to visualize the
    ethernet traffic in a timeline view. The original problem of the pcap24.c file was to visualize an ethernet frame stream,
    which contains multiple 24 bit wide samples.
    TODO:
     - pcap file partial loading, or continuous loading. Right now, the db_update reads the whole file, which is not efficient for large files.
     - navigation is very basic, we should implement a better navigation system, like zooming, panning, and following the current time.
*/
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <pcap.h>
#include <sys/types.h>
#include <limits.h>
#include "timelinedb.h"
#include "timelinedb_util.h"

#define MAXBUFF 500
#define MAX_TIMELINE_CHANNELS 80
#define MAX_TIMELINE_BUFS (MAX_TIMELINE_CHANNELS >> 3) // 8 channels per buffer
#define MAX_TIMELINE_SAMPLES (1000 * 1000)
#define DELAY_SCREEN_REFRESH (1000 / 30 )

#define MARGIN_TOP 100
#define MARGIN_BOTTOM 50
#define MARGIN_RIGHT 50
#define MARGIN_LEFT 5
#define LABEL_WIDTH 100

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
    int start_y;
    int height;
    int label_width;
    int right_margin;
} SignalCurvesView;

SignalCurve g_signal_curves[MAX_TIMELINE_CHANNELS];
SignalCurvesView g_signal_curves_view = {
    .count = MAX_TIMELINE_CHANNELS,
    .start_y = MARGIN_TOP,
    .height = 400, // Default height, can be always adjusted later
    .label_width = LABEL_WIDTH,  // Width for channel labels
    .right_margin = MARGIN_RIGHT // Right margin for scrollbar or other UI elements
};

pcap_t *g_pcap_handle;
const char* g_pcap_filename = NULL;

int g_screen_w = 800;
int g_screen_h = 600;
bool g_screen_size_changed = true;

TTF_Font *g_font_label = NULL;
TTF_Font *g_font_axis = NULL;

uint16_t g_number_of_channels = MAX_TIMELINE_CHANNELS; // Default number of channels
uint16_t g_number_of_visible_channels = MAX_TIMELINE_CHANNELS; // Default visible channels
uint16_t g_first_visible_channel = 0; // First visible channel index

// Global variables for zoom/pan/follow
float g_zoom_level = 1.0f; // 1.0 means full window
int g_view_offset = 0;
int g_follow_mode = 1;
bool g_aggregation_changed = false;
bool g_visible_channels_changed = false;

RawTimelineValuesBuf g_timeline_bufs[MAX_TIMELINE_BUFS];
RawTimelineValuesBuf g_timeline_min[MAX_TIMELINE_BUFS];
RawTimelineValuesBuf g_timeline_max[MAX_TIMELINE_BUFS];
TimelineDB g_timeline_db;
TimelineEvent g_timeline_events[MAX_TIMELINE_CHANNELS];

float g_sample_rate = 48000.0f; //move to buf later.
uint32_t g_total_valid_samples = 0; // Total valid samples in the current buffer, move to buf later.

uint32_t g_count_eth_ok = 0;
uint32_t g_count_eth_drop_mac = 0;
uint32_t g_count_eth_drop_unk = 0;

void db_init() {
    g_signal_curves_view.start_y = MARGIN_TOP;
    g_signal_curves_view.label_width = LABEL_WIDTH; // Width for channel labels
    g_signal_curves_view.right_margin = MARGIN_RIGHT; // Right margin for scrollbar or other UI elements

    for (int i = 0; i < MAX_TIMELINE_CHANNELS; i++) {
        g_timeline_events[i].id = i;
        static char name_buf[64];
        static char desc_buf[128];
        sprintf(name_buf, "signal%03d", i + 1);
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
        free_RawTimelineValuesBuf(&g_timeline_min[i]);
        free_RawTimelineValuesBuf(&g_timeline_max[i]);
    }
    for (int i = 0; i < MAX_TIMELINE_CHANNELS; i++) {
        free(g_timeline_events[i].name);
        free(g_timeline_events[i].description);
    }
}

/**
 * Parses the Ethernet payload for a single sample and stores it in the appropriate buffer.
 * @param payload Pointer to the Ethernet payload data.
 * @param num_channels Number of channels in the sample.
 * @param sample_idx Index of the sample to store in the buffer.
 * @return The next sample index after storing the current sample.
 */
int parse_ethPayload1(const u_char *payload, int num_channels, int sample_idx) {
    for (int ch = 0; ch < num_channels; ch++) {
        int buf_idx = ch / 8;
        int buf_ch = ch % 8;
        RawTimelineValuesBuf* buf = &g_timeline_bufs[buf_idx];
        int16_t* data = (int16_t*)buf->valueBuffer;
        if (!data) {
            fprintf(stderr, "Buffer not allocated for channel %d in buffer %d\n", ch, buf_idx);
            continue;
        }
        int offset = ch * 3;
        int32_t val = (int32_t)((payload[offset] << 16) | (payload[offset + 1] << 8) | payload[offset + 2]);
        if (val & 0x00800000ul) val |= ~0x00FFFFFFul; // Sign extend
        int16_t sval = (int16_t)(val >> 8); // Convert to 16-bit signed
        // store the sample in the buffer
        if (sample_idx < MAX_TIMELINE_SAMPLES) {
            data[sample_idx * 8 + buf_ch] = sval;
        }
    }
    for (int idx = 0 ; idx < num_channels/8; idx++) {
        RawTimelineValuesBuf* buf = &g_timeline_bufs[idx];
        if (sample_idx < MAX_TIMELINE_SAMPLES) {
            buf->nr_of_samples = sample_idx + 1;
        }else {
            buf->nr_of_samples = MAX_TIMELINE_SAMPLES;
        }
    }
    return sample_idx + 1; // Return next sample index
}

void db_update(Uint32 timestamp){
    (void)timestamp; // Not in use now

    if (g_pcap_handle) {
        pcap_close(g_pcap_handle);
        g_pcap_handle = NULL;
    }
    char errbuf[PCAP_ERRBUF_SIZE];
    g_pcap_handle = pcap_open_offline(g_pcap_filename, errbuf);
    if (!g_pcap_handle) {
        fprintf(stderr, "Failed to open pcap file: %s\n", errbuf);
        return;
    }

    // Parameters
    int skip_bytes = 14; // Ethernet header, set it for the first data index!
    const int bytes_per_channel = 3;    // 24 bit/channel

    // Buffer init
    for (int b = 0; b < MAX_TIMELINE_BUFS; b++) {
        RawTimelineValuesBuf* buf = &g_timeline_bufs[b];
        //int16_t* data = (int16_t*)buf->valueBuffer;
        buf->nr_of_samples = 0;
    }

    struct pcap_pkthdr* header;
    const u_char* pkt_data;
    int sample_idx = 0;

    // Track first and last timestamps
    struct timeval first_ts = {0};
    struct timeval last_ts = {0};
    int got_first_ts = 0;

    // iterate through a pcap file, while there are data and space in the buffer
    while (pcap_next_ex(g_pcap_handle, &header, &pkt_data) == 1) {
        // DSTMAC ellenőrzése
        int is_filter_ok = 1;
        for (int i = 0; i < 6; i++) {
            if (pkt_data[i] != 0xFF) {
                is_filter_ok = 0;
                break;
            }
        }
        const u_char srcmac_prefix[3] = {0x00, 0x04, 0xC4};
        for (int i = 0; i < 3; i++) {
            if (pkt_data[6 + i] != srcmac_prefix[i]) {
                is_filter_ok = 0;
                break;
            }
        }
        if (!is_filter_ok) {
            g_count_eth_drop_mac++;
            continue; // skip non-broadcast packets
        }

        uint16_t ethertype = (pkt_data[12] << 8) | pkt_data[13];
        switch (ethertype) {
        case 0x00DD: //Monitor
        case 0xDD00: //Ext
        case 0x04EE: //SQ
            skip_bytes = 14;
        break;
        default:
            g_count_eth_drop_unk++;
            fprintf(stderr, "Unknown Ethertype: 0x%04X\n", ethertype);
            is_filter_ok = 0;
        break;
        }
        if (!is_filter_ok) {
            continue;
        }
        int available_bytes = header->caplen - skip_bytes;
        int detected_channels = available_bytes / bytes_per_channel;

        if (detected_channels <= 0) {
            continue; // no valid sample data
        }
        g_count_eth_ok++;
        // Update global number of channels if needed
        if (g_number_of_channels == 0 || g_number_of_channels > detected_channels) {
            g_number_of_channels = detected_channels;
        }
        // Use the minimum between g_number_of_channels and detected_channels for this packet
        int use_channels = g_number_of_channels;
        if (use_channels > detected_channels) use_channels = detected_channels;

        const u_char* payload = pkt_data + skip_bytes;
        // read the sample data from the payload
        sample_idx = parse_ethPayload1(payload, use_channels, sample_idx);
        // Track first and last timestamps
        if (!got_first_ts) {
            first_ts = header->ts;
            got_first_ts = 1;
        }
        last_ts = header->ts;
        if (sample_idx >= MAX_TIMELINE_SAMPLES) break;
    }
    if (g_number_of_channels < g_number_of_visible_channels) {
        g_number_of_visible_channels = g_number_of_channels;
    }
    if (g_first_visible_channel + g_number_of_visible_channels > g_number_of_channels) {
        g_first_visible_channel = g_number_of_channels - g_number_of_visible_channels;
    }
    // Apply zoom/pan/follow: select visible sample range
    int total_samples = sample_idx;
    g_total_valid_samples = total_samples;
    int visible_samples = (int)(total_samples / g_zoom_level);
    int start_sample = 0;
    if (g_follow_mode) {
        start_sample = total_samples - visible_samples;
        if (start_sample < 0) start_sample = 0;
        if (start_sample + visible_samples > total_samples) {
            start_sample = total_samples - visible_samples;
            if (start_sample < 0) start_sample = 0;
        }
        g_view_offset = start_sample; // Update view offset to match follow mode
    } else {
        start_sample = g_view_offset;
    }

    // After reading packets, compute total_time_sec for each buffer.
    double total_time_sec = 0.0;
    int sample_count = sample_idx;
    if (got_first_ts && sample_count > 1) {
        total_time_sec = (last_ts.tv_sec - first_ts.tv_sec) + (last_ts.tv_usec - first_ts.tv_usec) / 1e6;
    } else if (got_first_ts && sample_count == 1) {
        total_time_sec = 0.0;
    }
    g_sample_rate = (float)sample_count / total_time_sec; // Update sample rate based on actual samples read

    for (int b = 0; b < MAX_TIMELINE_BUFS; b++) {
        RawTimelineValuesBuf* buf = &g_timeline_bufs[b];
        buf->total_time_sec = total_time_sec;
        buf->time_step = (uint32_t)(total_time_sec * 1000000000.0 / sample_count); // in microseconds
        buf->time_exponent = -9; // microseconds
    }

    // For each buffer, copy only the visible samples into the buffer's valueBuffer (in-place, so that aggregation uses only visible samples)
    for (int b = 0; b < MAX_TIMELINE_BUFS; b++) {
        RawTimelineValuesBuf* buf = &g_timeline_bufs[b];
        int16_t* data = (int16_t*)buf->valueBuffer;
        if (buf->nr_of_samples > 0) {
            int nsamp = buf->nr_of_samples;
            int nchan = buf->nr_of_channels;
            int ncopy = visible_samples;
            if (start_sample + ncopy > nsamp) ncopy = nsamp - start_sample;
            if (ncopy < 0) ncopy = 0;
            if (ncopy > 0 && (start_sample > 0 || ncopy < nsamp)) {
                // Move only visible samples to the front of the buffer
                for (int i = 0; i < ncopy; ++i) {
                    for (int ch = 0; ch < nchan; ++ch) {
                        data[i * nchan + ch] = data[(start_sample + i) * nchan + ch];
                    }
                }
                buf->nr_of_samples = ncopy;
            }
        }
    }
}

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
void free_fonts() {
    if (g_font_label) {
        TTF_CloseFont(g_font_label);
        g_font_label = NULL;
    }
    if (g_font_axis) {
        TTF_CloseFont(g_font_axis);
        g_font_axis = NULL;
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

    uint32_t start_x = g_signal_curves_view.label_width;

    // Draw a horizontal separator line between signal areas
    SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
    SDL_RenderDrawLine(renderer,
        0,
        curve->offsety ,
        g_screen_w,
        curve->offsety);

    // Restore curve color
    SDL_SetRenderDrawColor(renderer, (curve->color >> 16) & 0xFF, (curve->color >> 8) & 0xFF, curve->color & 0xFF, 255);

    // Draw signal name (if using SDL_ttf, add actual text rendering here)
    SDL_DrawText(renderer, curve->event->name, 0, curve->offsety);

    uint32_t drawable_width = g_screen_w - g_signal_curves_view.label_width - g_signal_curves_view.right_margin;
    uint16_t *minp= (uint16_t *)min_buf->valueBuffer;
    uint16_t *maxp= (uint16_t *)max_buf->valueBuffer;
    minp+=curve->channelidx;
    maxp+=curve->channelidx;
    uint8_t d = min_buf->nr_of_channels;
    uint32_t x = start_x;
    for (uint32_t i = 0; i < nr_of_samples - 1; i++) {
        int16_t v1, v2;
        v1 = *minp;
        v2 = *maxp;
        minp+= d; maxp+= d;
        x = start_x + i * drawable_width / nr_of_samples;
        SDL_RenderDrawLine(renderer,
            x,
            curve->offsety - (int)(v1 * curve->scale),
            start_x + (i + 1) * drawable_width / nr_of_samples,
            curve->offsety - (int)(v2 * curve->scale));
        x = start_x + (i + 1) * drawable_width / nr_of_samples;
    }
    if (x < drawable_width) {
        SDL_Rect fillr =  {
            x,
            curve->offsety - (int)(curve->height),
            drawable_width-x,
            (int)(curve->height * curve->scale)};
        SDL_SetRenderDrawColor(renderer, (curve->color >> 16) & 0xFF, (curve->color >> 8) & 0xFF, curve->color & 0xFF, 255);
        SDL_RenderFillRect(renderer, &fillr);
    }
}

void draw_time_label(SDL_Renderer *renderer, int x, int y, const char* label) {
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
    SDL_Rect dstRect = { x - text_w / 2, 50+y, text_w, text_h };
    SDL_RenderCopy(renderer, textTexture, NULL, &dstRect);
    SDL_DestroyTexture(textTexture);
    SDL_FreeSurface(textSurface);
}

void draw_timeline_overview(SDL_Renderer *renderer, const RawTimelineValuesBuf *buf, RawTimelineValuesBuf *aggr) {
    (void)buf; // Unused, we use aggr for drawing
    const int bar_height = 8;
    const int bar_y = 50 - bar_height - 2;

    uint32_t total_samples = g_total_valid_samples;//buf->nr_of_samples;
    uint32_t view_offset = g_view_offset;
    uint32_t view_samples = aggr->nr_of_samples;

    float left_ratio = (float)view_offset / total_samples;
    float middle_ratio = (float)view_samples / total_samples;
    //float right_ratio = 1.0f - left_ratio - middle_ratio;

    int bar_width = g_screen_w - g_signal_curves_view.right_margin;

    SDL_Rect left_rect   = { 0, bar_y, (int)(bar_width * left_ratio), bar_height };
    SDL_Rect middle_rect = { left_rect.w, bar_y, (int)(bar_width * middle_ratio), bar_height };
    SDL_Rect right_rect  = { left_rect.w + middle_rect.w, bar_y, bar_width - (left_rect.w + middle_rect.w), bar_height };

    // background
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
    SDL_RenderFillRect(renderer, &(SDL_Rect){0, bar_y, bar_width, bar_height});

    // colors
    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);  SDL_RenderFillRect(renderer, &left_rect);
    SDL_SetRenderDrawColor(renderer, 100, 200, 255, 255); SDL_RenderFillRect(renderer, &middle_rect);
    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);  SDL_RenderFillRect(renderer, &right_rect);
}

// Draws dynamic timeline axis with ticks and labels based on zoom and offset
void draw_time_axis(SDL_Renderer *renderer, Uint32 timestamp) {
    (void)timestamp; // Unused
    const int top = 50; // Top position for the time axis
    const int axis_height = g_signal_curves_view.start_y - top;
    const int label_width = g_signal_curves_view.label_width;
    const int right_margin = g_signal_curves_view.right_margin;
    const int plot_area_w = g_screen_w - label_width - right_margin;

    // Draw axis background
    SDL_SetRenderDrawColor(renderer, 16, 16, 16, 255);
    SDL_Rect axis_rect = {0, top, g_screen_w, axis_height};
    SDL_RenderFillRect(renderer, &axis_rect);
    
    // Use the first buffer as reference
    RawTimelineValuesBuf* ref_buf = g_signal_curves[0].buf;
    RawTimelineValuesBuf* ref_buf_min = g_signal_curves[0].min_buf;

    // How many original samples are visible on-screen?
    uint32_t total_samples = 0;
    for (int i = 0; i < MAX_TIMELINE_BUFS; i++) {
        uint32_t sampleshere = g_timeline_bufs[i].nr_of_samples;
        if (sampleshere > total_samples)
            total_samples = sampleshere;
    }

    // Estimate the visible sample range (if following, offset is at the end)
    int visible_samples = (int)( ref_buf_min->nr_of_samples / g_zoom_level);
    int inOffset = (int)(g_view_offset / g_zoom_level);
    if (total_samples & 0x80000000ul) {
        fprintf(stderr, "Error: Total samples number can not be indexed on signed int.\n");
        return;
    }

    int start_sample = g_follow_mode ? ((int)total_samples - visible_samples) : inOffset;
    if (start_sample < 0) start_sample = 0;
    // Each pixel represents how many samples?
    float samples_per_pixel = (visible_samples > 0) ? (float)visible_samples / plot_area_w : 1.0f;
    float pixels_per_sample = (visible_samples > 0) ? (float)plot_area_w / visible_samples : 1.0f;

    // Minimum pixel spacing between ticks (labels should not overlap)
    const int min_tick_pixel_spacing = 80;
    // Find a suitable tick spacing in samples
    // Try powers of 10, 2*10^x, 5*10^x for nice labels
    int tick_spacing_samples = 1;
    float tick_pixel_spacing = 0.0f;
    int tick_spacing_options[] = {1,2,5};
    int mult = 1;
    while (1) {
        for (int i=0;i<3;++i) {
            int spacing = tick_spacing_options[i]*mult;
            tick_pixel_spacing = spacing * pixels_per_sample;
            if (tick_pixel_spacing >= min_tick_pixel_spacing) {
                tick_spacing_samples = spacing;
                goto found_spacing;
            }
        }
        mult *= 10;
        if (mult > 100000000) break;
    }
found_spacing:;

    double time_ms = start_sample * ref_buf->time_step * pow(10.0, ref_buf->time_exponent);
    char label[64];
    snprintf(label, sizeof(label), "%.3f sec", time_ms);
    draw_time_label(renderer, label_width/2, 0, label);
    snprintf(label, sizeof(label), "%d sample", start_sample);
    draw_time_label(renderer, label_width/2, 16, label);
    double srate = 0;
    const char* srate_unit = "Hz";
    getEngineeringSampleRateFrequency(ref_buf, &srate, &srate_unit);
    snprintf(label, sizeof(label), "Freq: %d %s", (int)srate, srate_unit);
    draw_time_label(renderer, label_width/2, 32, label);

    // Decide: show sample index or milliseconds?
    int show_ms = (g_sample_rate > 10.0f && samples_per_pixel < g_sample_rate/1000.0f);

    // Compute the first visible tick (aligned to tick_spacing_samples)
    int first_tick_sample = ((start_sample + tick_spacing_samples - 1) / tick_spacing_samples) * tick_spacing_samples;
    int last_visible_sample = start_sample + visible_samples;

    // Draw ticks and labels
    for (int tick_sample = first_tick_sample; tick_sample <= last_visible_sample; tick_sample += tick_spacing_samples) {
        int px = label_width + (int)((tick_sample - start_sample) * pixels_per_sample);
        if (px >= g_screen_w - right_margin) break;
        // Major tick every N ticks
        int is_major = ((tick_sample / tick_spacing_samples) % 5 == 0);
        if (is_major)
            SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255);
        else
            SDL_SetRenderDrawColor(renderer, 64, 64, 64, 255);
        SDL_RenderDrawLine(renderer, px, top, px, g_screen_h - 50); // Draw vertical line from top to bottom
        // Draw label for major ticks only
        if (1) {
            char label[32];
            if (show_ms) {
                float t_ms = (float)tick_sample * 1000.0f / g_sample_rate;
                if (t_ms < 1.0f)
                    snprintf(label, sizeof(label), "%.2f ms", t_ms);
                else if (t_ms < 10.0f)
                    snprintf(label, sizeof(label), "%.1f ms", t_ms);
                else
                    snprintf(label, sizeof(label), "%.0f ms", t_ms);
            } else {
                snprintf(label, sizeof(label), "%.1f s", ( tick_sample / g_sample_rate));
            }
            draw_time_label(renderer, px, is_major?0:16, label);
        }
    }
}
void draw_curves(SDL_Renderer* renderer) {
    for (int i = 0; i < g_number_of_visible_channels; i++) {
        int idx = i + g_first_visible_channel;
        if (g_signal_curves[idx].buf && g_signal_curves[idx].buf->valueBuffer) {
            draw_one_curve(renderer, &g_signal_curves[idx]);
        }
    }
}

void realloc_RawTimelineValuesBufs(RawTimelineValuesBuf *buf, uint32_t new_w) {
    free_RawTimelineValuesBuf(buf);
    alloc_RawTimelineValuesBuf(buf, new_w, // 8, 16, 16, TR_SIMD_sint16x8);
        buf->nr_of_channels, buf->bitwidth, buf->bytes_per_sample, buf->value_type);
}

void screen_update(Uint32 timestamp, SDL_Renderer* renderer) {
    (void)timestamp; // Unused in this example
    if (g_screen_size_changed) {
        g_screen_size_changed = false;
        g_visible_channels_changed = true;
        for (int i = 0; i < MAX_TIMELINE_BUFS; i++) {
            realloc_RawTimelineValuesBufs(&g_timeline_min[i], g_screen_w);
            realloc_RawTimelineValuesBufs(&g_timeline_max[i], g_screen_w);
            prepare_AggregationMinMax(&g_timeline_bufs[i], &g_timeline_min[0], &g_timeline_max[0], g_screen_w);
        }
        g_signal_curves_view.height = g_screen_h - g_signal_curves_view.start_y - 50; // 50px for bottom margin
    }
    if (g_visible_channels_changed) {
        g_visible_channels_changed = false;
        for (int i = 0; i < g_number_of_visible_channels; i++) {
            int idx = i + g_first_visible_channel;
            uint32_t h = g_signal_curves_view.height / g_number_of_visible_channels;  //todo: visible number of channels
            g_signal_curves[idx].height = h;
            g_signal_curves[idx].offsety = g_signal_curves_view.start_y + i * h + h / 2;
            g_signal_curves[idx].scale = (float)h / 65536.0f; // Scale to fit in height
        }     
    }
    // Dynamic aggregation: aggregation width depends on zoom level
    int agg_samples = g_screen_w;
    int inSamples = (int)(agg_samples / g_zoom_level);
    int inOffset = (int)(g_view_offset / g_zoom_level);
    double window_time_sec = g_timeline_bufs[0].total_time_sec / g_zoom_level;
    int exp= g_timeline_bufs[0].time_exponent;
    int tsteps = g_timeline_bufs[0].time_step;
    if (inSamples > 0) {
        double tstep = window_time_sec / inSamples;
        exp = (int)floor(log10(tstep)/3)*3;
        if (exp < -12) exp = -12;
        tsteps = (int)round(tstep * pow(10, -exp));
    }
    for (int i = 0; i < MAX_TIMELINE_BUFS; i++) {
        aggregate_MinMax(&g_timeline_bufs[i], &g_timeline_min[i], &g_timeline_max[i], inSamples, inOffset);
        g_timeline_min[i].total_time_sec = window_time_sec;
        g_timeline_min[i].time_step = tsteps;
        g_timeline_min[i].time_exponent = exp;
        g_timeline_max[i].total_time_sec = window_time_sec;
        g_timeline_max[i].time_step = tsteps;
        g_timeline_max[i].time_exponent = exp;
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
    draw_timeline_overview(renderer, &g_timeline_bufs[0], &g_timeline_min[0]);

    // --- Draw follow mode status overlay ---
    char follow_status[32];
    snprintf(follow_status, sizeof(follow_status), "Follow mode: %s", g_follow_mode ? "ON" : "OFF");
    SDL_DrawText(renderer, follow_status, 10, 10); // Adjust coordinates as needed
    // --- End overlay ---

    SDL_RenderPresent(renderer);
}

void on_timer_tick(Uint32 timestamp, SDL_Renderer* renderer) {
    if (g_follow_mode || g_aggregation_changed) {
        g_aggregation_changed = false;
        db_update(timestamp);
    }
    screen_update(timestamp, renderer);
}

void processWheel(int dy, bool zoom, int mouse_x) {
    if (mouse_x > g_signal_curves_view.label_width) {
        // existing zoom or scroll logic for the graph area
        if (zoom) {
            if (dy > 0) g_zoom_level *= 1.1f;
            else if (dy < 0) g_zoom_level /= 1.1f;
            if (g_zoom_level < 0.0001f) g_zoom_level = 0.0001f;
            g_aggregation_changed = true;
        } else {
            g_view_offset += dy * 1000.0 * g_zoom_level; // Adjust the offset based on zoom level
            if (g_view_offset < 0) g_view_offset = 0;
            g_follow_mode = 0;
            g_aggregation_changed = true;
        }
    } else {
        if (zoom) {
            if (dy > 0 && g_number_of_visible_channels < g_number_of_channels) g_number_of_visible_channels++;
            else if (dy < 0 && g_number_of_visible_channels > 1) g_number_of_visible_channels--;
            g_visible_channels_changed = true;
        }else{
            if (dy > 0 && g_first_visible_channel > 0) {
                g_first_visible_channel--;
                g_aggregation_changed = true;
            } else if (dy < 0 && g_first_visible_channel + g_number_of_visible_channels < g_number_of_channels) {
                g_first_visible_channel++;
                g_aggregation_changed = true;
            }
            g_visible_channels_changed = true;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <pcap_file>\n", argv[0]);
        return 1;
    }
    g_pcap_filename = argv[1];
    g_pcap_handle = NULL;
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
                    g_screen_size_changed = true;
                    screen_update(now, renderer);
                }
            }
            if (event.type == SDL_MOUSEWHEEL) {
                const Uint8 *keystate = SDL_GetKeyboardState(NULL);
                bool zoom = (keystate[SDL_SCANCODE_LSHIFT] || keystate[SDL_SCANCODE_RSHIFT]);
                int mouse_x = 0, mouse_y = 0;
                SDL_GetMouseState(&mouse_x, &mouse_y);
                processWheel(event.wheel.y, zoom, mouse_x);
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_f) {
                g_follow_mode = !g_follow_mode;
                g_aggregation_changed = true;
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
    pcap_close(g_pcap_handle);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    free_fonts(); 
    TTF_Quit();
    SDL_Quit();
    db_free();
    return 0;
}
