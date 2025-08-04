#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <pcap.h>
#include <string.h>
#include <time.h>

int main(int argc, char *argv[]) {
    double duration_sec = 2;
    
    if (argc > 1) {
        duration_sec = atof(argv[1]);
    }
    
    const char *filename = "sine32ch.pcap";
    const int sample_rate = 48000;
    const int total_samples = sample_rate * duration_sec;
    const int num_channels = 32;
    const int start_payload = 14;
    const int frame_payload_bytes = num_channels * 3;
    const int frame_size = start_payload + frame_payload_bytes;
    const uint8_t dst_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    const uint8_t src_mac[6] = {0x00, 0x04, 0xc4, 0x78, 0x9a, 0xbc};
    const uint16_t ethertype = 0x00DD; // Custom Ethertype for AH SQ Monitor
    const double freq_start = 15000.0;
    const double freq_end = 10.0;
    const double am_freq = 10.0;

    pcap_t *pcap = pcap_open_dead(DLT_EN10MB, 65535);
    pcap_dumper_t *dumper = pcap_dump_open(pcap, filename);
    if (!dumper) {
        fprintf(stderr, "Could not open output file.\n");
        return 1;
    }

    double freqs[num_channels];
    for (int i = 0; i < num_channels; ++i)
        freqs[i] = freq_start - (freq_start - freq_end) * i / (num_channels - 1);

    for (int s = 0; s < total_samples; ++s) {
        uint8_t packet[1500] = {0};
        memcpy(packet, dst_mac, 6);
        memcpy(packet + 6, src_mac, 6);
        packet[12] = (ethertype >> 8) & 0xff;
        packet[13] = ethertype & 0xff;

        for (int ch = 0; ch < num_channels; ++ch) {
            double t = (double)s / sample_rate;
            double am = 0.6 + 0.39 * sin(2 * M_PI * am_freq * t);
            double val = am * sin(2 * M_PI * freqs[ch] * t);
            if (val < -1.0) val = -1.0;
            else if (val > 1.0) val = 1.0;
            int32_t sample = (int32_t)(val * 0x007ffffful);  // 24-bit signed
            int offset = start_payload + ch * 3;
            packet[offset + 0] = (sample >> 16) & 0xff;
            packet[offset + 1] = (sample >> 8) & 0xff;
            packet[offset + 2] = sample & 0xff;
        }

        struct pcap_pkthdr hdr;
        double t = (double)s / sample_rate;
        hdr.ts.tv_sec = (time_t)t;
        hdr.ts.tv_usec = (suseconds_t)((t - (time_t)t) * 1e6);
        hdr.caplen = frame_size;
        hdr.len = frame_size;
        pcap_dump((u_char *)dumper, &hdr, packet);
    }

    pcap_dump_close(dumper);
    pcap_close(pcap);
    return 0;
}