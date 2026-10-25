
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define S1_BAUD          2400u
#define S1_FLEN          63
#define S1_DATLEN        32
#define S1_CRC_POLY      0x8408u

static const uint8_t s1_whitening[64] = {
    0x0F,0x70,0xB3,0x6F,0x43,0x98,0x48,0xAE,
    0xBC,0x97,0x38,0x1D,0xD3,0xD4,0xA0,0x55,
    0x7D,0x68,0x37,0x6D,0x60,0xBB,0xE3,0xCD,
    0x35,0xC6,0x8B,0xFA,0x58,0xA6,0x30,0x19,
    0x95,0x93,0xF6,0x92,0x6F,0xCB,0x50,0xA2,
    0x76,0x5E,0xC3,0x54,0xE4,0x31,0x08,0x04,
    0x46,0x47,0x56,0xC7,0x12,0xA3,0x67,0xCF,
    0x16,0xE5,0x20,0x99,0xD1,0xF7,0x83,0xFE,
};
static const uint32_t s1_golay_H[12] = {
    0x00800B71, 0x004006E3, 0x00200DC5, 0x00100B8B,
    0x00080717, 0x00040E2D, 0x00020C5B, 0x000108B7,
    0x0000816F, 0x000042DD, 0x000025B9, 0x00001FFE,
};

static int s1_golay_decode(uint32_t *cw, int *numcorrected) {
    uint32_t S = 0, w, x;
    int i, j;
    *numcorrected = 0;
    for (i = 0; i < 12; i++) {
        S = 2 * S + (__builtin_popcount(*cw & s1_golay_H[i]) % 2);
    }
    w = (uint32_t)__builtin_popcount(S);
    if (w > 0) {
        if (w <= 3) {
            *cw ^= (S << 12);
            *numcorrected = (int)w;
        } else {
            for (j = 0; j < 12; j++) {
                x = S ^ (s1_golay_H[j] & 0xFFFu);
                w = (uint32_t)__builtin_popcount(x);
                if (w <= 2) {
                    *cw ^= (x << 12) + (1u << (11 - j));
                    *numcorrected = 1 + (int)w;
                }
            }
            if (*numcorrected == 0) {
                uint32_t S2 = 0;
                for (i = 0; i < 12; i++) {
                    S2 = 2 * S2 + (__builtin_popcount(S & (s1_golay_H[i] & 0xFFFu)) % 2);
                }
                w = (uint32_t)__builtin_popcount(S2);
                if (w <= 3) {
                    *cw ^= S2;
                    *numcorrected = (int)w;
                } else {
                    for (j = 0; j < 12; j++) {
                        x = S2 ^ (s1_golay_H[j] & 0xFFFu);
                        w = (uint32_t)__builtin_popcount(x);
                        if (w <= 2) {
                            *cw ^= x + (1u << (23 - j));
                            *numcorrected = 1 + (int)w;
                        }
                    }
                }
            }
        }
    }
    S = 0;
    for (i = 0; i < 12; i++) {
        S = 2 * S + (__builtin_popcount(*cw & s1_golay_H[i]) % 2);
    }
    return (S == 0);
}
static int s1_crc_check(const uint8_t *b, int b_len, int *match_pos) {
    uint16_t crc = 0xFFFFu;
    int i, j;
    for (i = 0; i < b_len - 2; i++) {
        for (j = 0; j < 8; j++) {
            int bit = (crc & 1) ^ ((b[i] >> j) & 1);
            crc = bit ? (crc >> 1) ^ S1_CRC_POLY : (crc >> 1);
        }
        uint16_t rxcrc = (uint16_t)(b[i + 1] * 256 + b[i + 2]);
        if (crc == rxcrc) {
            if (match_pos) *match_pos = i;
            return 1;
        }
    }
    return 0;
}

static uint32_t s1_readbits(const uint8_t *buf, int *pos, int n) {
    uint32_t data = 0;
    int i;
    for (i = 0; i < n; i++) {
        int p = *pos + i;
        uint32_t by = buf[3 + (p / 8)];
        uint32_t bi = (by >> (7 - (p % 8))) & 1u;
        data = 2 * data + bi;
    }
    *pos += n;
    return data;
}

typedef struct {
    int have_id;
    uint32_t id;
    int have_sid;
    uint32_t sid;
    int have_temperature;
    float temperature_c;
    int have_humidity;
    float humidity_percent;
    int have_pressure;
    float pressure_hpa;
    int have_gps;
    int have_alt;
    float lat, lon, alt_m;
    int have_speed;
    float speed_kmh;
    float direction_raw;
    int have_vbat;
    float vbat_v;
    int have_lux;
    uint32_t lux_raw;
    int history_count;
    int golay_corrected;
    int golay_failed;
    int have_vel_v;
    float vel_v_mps;
} S1_Payload;
static int32_t g_lat_ref_raw = 0;
static int32_t g_lon_ref_raw = 0;
static double g_ref_time_s = 0.0;
static int g_have_ref = 0;
static double g_sample_seconds = 0.0;
static int g_json_mode = 0;
static int g_verbose = 0;
static double g_tx_frequency_hz = 0.0;
static uint32_t g_frame_number = 0;
static int g_have_prev_alt = 0;
static float g_prev_alt_m = 0.0f;
static double g_prev_alt_time_s = 0.0;

static void iso8601_now(char *buf, size_t buflen) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_utc;
    gmtime_r(&ts.tv_sec, &tm_utc);
    int ms = (int)(ts.tv_nsec / 1000000);
    snprintf(buf, buflen, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
             tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec, ms);
}
static void s1_decode_extra(const uint8_t *rxdat, int *pos, S1_Payload *out) {
    int scan = 1;
    int guard = 0;
    while (scan && guard < 32) {
        guard++;
        uint32_t opcode = s1_readbits(rxdat, pos, 5);
        uint32_t format = s1_readbits(rxdat, pos, 2);
        uint32_t field_length = 0;
        if (format == 0) {
            field_length = 4;
        } else if (format == 1) {
            field_length = 8;
            if (s1_readbits(rxdat, pos, 1) == 1) field_length = 12;
        } else if (format == 2) {
            field_length = 16;
            if (s1_readbits(rxdat, pos, 1) == 1) field_length = 0;
        }
        if (field_length == 0) break;
        uint32_t value = s1_readbits(rxdat, pos, field_length);
        scan = s1_readbits(rxdat, pos, 1);
        if (g_verbose || getenv("S1_DEBUG")) {
            fprintf(stderr, "S1 EXTRA opcode=0x%02X format=%u bits=%u value=%u\n",
                    opcode, format, field_length, value);
        }
        if (opcode == 0x09) {
            out->vbat_v = 2.56f + value * 0.01f;
            out->have_vbat = 1;
        }
    }
}
static int s1_parse_payload(const uint8_t *rxdat, S1_Payload *out) {
    memset(out, 0, sizeof(*out));
    int opcode = rxdat[1] & 0x0F;
    if (opcode != 0x0C) return 0;
    int pos = 8;
    int haveID = (rxdat[1] & 0xF0) == 0;
    if (haveID) {
        out->id = s1_readbits(rxdat, &pos, 16);
        out->have_id = 1;
    } else {
        out->sid = s1_readbits(rxdat, &pos, 4);
        out->have_sid = 1;
    }
    uint8_t flags = rxdat[3];
    int haveBASE1 = flags != 0;
    int haveBASE2 = (flags & 0xF0) != 0;
    int haveGPS   = (flags >> 7) & 1;
    int haveLUX   = (flags >> 6) & 1;
    int haveHIST  = (flags >> 5) & 1;
    int haveEXTRA = (flags >> 1) & 1;
    int haveHUM = (rxdat[0] == 0x04) || (rxdat[0] == 0x0A) || (rxdat[0] == 0x3E) || (rxdat[0] == 0x45);
    s1_readbits(rxdat, &pos, 4);

    if (haveBASE1) {
        int32_t tebits = (int32_t)s1_readbits(rxdat, &pos, 14);
        out->temperature_c = -10.0f + (tebits - 8192) / 100.0f;
        out->have_temperature = 1;
        if (s1_readbits(rxdat, &pos, 2) == 1) {
            if (s1_readbits(rxdat, &pos, 1) == 1) {
                s1_readbits(rxdat, &pos, 8);
            }
        }
        if (haveHUM) {
            out->humidity_percent = 0.05f * s1_readbits(rxdat, &pos, 11);
            out->have_humidity = 1;
        }
    }
    if (haveBASE2) {
        uint32_t pabits = s1_readbits(rxdat, &pos, 16);
        out->pressure_hpa = pabits * 0.02f;
        out->have_pressure = 1;
    }
    if (haveGPS) {
        int haveSPDANG = s1_readbits(rxdat, &pos, 1);
        if (s1_readbits(rxdat, &pos, 1) == 1) {
            if (s1_readbits(rxdat, &pos, 1) == 0) {
                uint32_t altbits = s1_readbits(rxdat, &pos, 14);
                out->alt_m = (float)altbits;
                out->have_alt = 1;
            }

        }
        if (haveSPDANG) {
            out->speed_kmh = s1_readbits(rxdat, &pos, 11) * 0.18f;
            float direction_rad = s1_readbits(rxdat, &pos, 12) * 0.01f;
            out->direction_raw = fmodf(direction_rad * 180.0f / (float)M_PI, 360.0f);
            if (out->direction_raw < 0.0f) out->direction_raw += 360.0f;
            out->have_speed = 1;
        }
        if (s1_readbits(rxdat, &pos, 1) == 1) {
            s1_readbits(rxdat, &pos, 8);
        }
        int have_pos = 0;
        uint32_t posfmt = s1_readbits(rxdat, &pos, 2);
        switch (posfmt) {
            case 0:
                s1_readbits(rxdat, &pos, 20);
                break;
            case 1: {
                int32_t lat_d = (int32_t)s1_readbits(rxdat, &pos, 14);
                int32_t lon_d = (int32_t)s1_readbits(rxdat, &pos, 14);
                if (g_have_ref && (g_sample_seconds - g_ref_time_s) < 20.0) {
                    if (lat_d < 10000 && lon_d < 10000) {
                        int32_t lat_ref_mod = g_lat_ref_raw % 10000;
                        if (abs(lat_d - lat_ref_mod) >= 5000) lat_d += (lat_d < lat_ref_mod) ? 10000 : -10000;
                        g_lat_ref_raw = 10000 * (g_lat_ref_raw / 10000) + lat_d;
                        int32_t lon_ref_mod = g_lon_ref_raw % 10000;
                        if (abs(lon_d - lon_ref_mod) >= 5000) lon_d += (lon_d < lon_ref_mod) ? 10000 : -10000;
                        g_lon_ref_raw = 10000 * (g_lon_ref_raw / 10000) + lon_d;
                        have_pos = 1;
                    }
                }
                break;
            }
            case 2: {
                int32_t lat_d = (int32_t)s1_readbits(rxdat, &pos, 20);
                int32_t lon_d = (int32_t)s1_readbits(rxdat, &pos, 20);
                if (g_have_ref && (g_sample_seconds - g_ref_time_s) < 50.0) {
                    if (lat_d < 1000000 && lon_d < 1000000) {
                        int32_t lat_ref_mod = g_lat_ref_raw % 1000000;
                        if (abs(lat_d - lat_ref_mod) >= 500000) lat_d += (lat_d < lat_ref_mod) ? 1000000 : -1000000;
                        g_lat_ref_raw = 1000000 * (g_lat_ref_raw / 1000000) + lat_d;
                        int32_t lon_ref_mod = g_lon_ref_raw % 1000000;
                        if (abs(lon_d - lon_ref_mod) >= 500000) lon_d += (lon_d < lon_ref_mod) ? 1000000 : -1000000;
                        g_lon_ref_raw = 1000000 * (g_lon_ref_raw / 1000000) + lon_d;

                        have_pos = 1;
                    }
                }
                break;
            }
            case 3:
                g_lat_ref_raw = (int32_t)s1_readbits(rxdat, &pos, 28);
                g_lon_ref_raw = (int32_t)s1_readbits(rxdat, &pos, 29);
                g_ref_time_s = g_sample_seconds;
                g_have_ref = 1;
                have_pos = 1;
                break;
        }
        if (have_pos) {
            int32_t latdeg = (g_lat_ref_raw - 90000000L) / 1000000L;
            float latmin = (g_lat_ref_raw - 90000000L - 1000000L * latdeg) / 10000.0f;
            out->lat = latdeg + latmin / 60.0f;

            int32_t londeg = (g_lon_ref_raw - 180000000L) / 1000000L;
            float lonmin = (g_lon_ref_raw - 180000000L - 1000000L * londeg) / 10000.0f;
            out->lon = londeg + lonmin / 60.0f;
            out->have_gps = 1;
        }
    }
    if (haveLUX) {
        out->lux_raw = s1_readbits(rxdat, &pos, 12);
        out->have_lux = 1;
    }
    if (haveHIST) {
        int idx = 1;
        while (idx < 10) {
            if (haveGPS) {
                s1_readbits(rxdat, &pos, 8);
                s1_readbits(rxdat, &pos, 12);
            }
            s1_readbits(rxdat, &pos, 8);
            if (haveHUM) s1_readbits(rxdat, &pos, 8);
            out->history_count = idx;
            if (s1_readbits(rxdat, &pos, 1) == 0) break;
            idx++;
        }
        if ((g_verbose || getenv("S1_DEBUG")) && out->history_count > 0) {
            fprintf(stderr, "S1 HIST entries=%d\n", out->history_count);
        }
    }
    if (haveEXTRA) {
        s1_decode_extra(rxdat, &pos, out);
    }
    return 1;
}


static int s1_process_frame(const uint8_t *rxbuf_whitened) {
    uint8_t rxdat[S1_DATLEN];
    int i;
    int golay_corrected = 0;
    int golay_failed = 0;
    for (i = 0; i < S1_FLEN; i += 3) {
        uint32_t cw = ((uint32_t)rxbuf_whitened[i] << 16)
                    | ((uint32_t)rxbuf_whitened[i + 1] << 8)
                    |  (uint32_t)rxbuf_whitened[i + 2];
        int numcorrected = 0;
        if (!s1_golay_decode(&cw, &numcorrected)) {
            golay_failed++;
        }
        golay_corrected += numcorrected;
        if ((i % 2) == 0) {
            rxdat[(i / 2) + 0] = (uint8_t)((cw >> 16) & 0xFF);
            rxdat[(i / 2) + 1] = (uint8_t)(((cw >> 12) & 0x0F) << 4);
        } else {
            rxdat[(i / 2) + 0] |= (uint8_t)((cw >> 20) & 0x0F);
            rxdat[(i / 2) + 1] = (uint8_t)((cw >> 12) & 0xFF);
        }
    }
    if (g_verbose || getenv("S1_DEBUG")) {
        fprintf(stderr, "DEBUG golay_corrected=%d golay_failed=%d rxdat:",
                golay_corrected, golay_failed);
        for (i = 0; i < S1_DATLEN; i++) fprintf(stderr, " %02X", rxdat[i]);
        fprintf(stderr, "\n");
    }
    int crc_pos = -1;
    if (!s1_crc_check(rxdat, S1_DATLEN - 1, &crc_pos)) {
        memcpy(rxdat, rxbuf_whitened, S1_DATLEN);
        if (!s1_crc_check(rxdat, S1_DATLEN - 1, &crc_pos)) return 0;
        golay_corrected = 0;
        golay_failed = 0;
        if (g_verbose || getenv("S1_DEBUG")) {
            fprintf(stderr, "DEBUG uncoded frame rxdat:");
            for (i = 0; i < S1_DATLEN; i++) fprintf(stderr, " %02X", rxdat[i]);
            fprintf(stderr, "\n");
        }
    }
    if (g_verbose || getenv("S1_DEBUG")) {
        fprintf(stderr, "DEBUG crc matched at byte index %d (payload length ~%d bytes)\n", crc_pos, crc_pos + 3);
    }
    S1_Payload p;
    if (s1_parse_payload(rxdat, &p)) {
        p.golay_corrected = golay_corrected;
        p.golay_failed = golay_failed;
        if (p.have_alt) {
            if (g_have_prev_alt) {
                double dt = g_sample_seconds - g_prev_alt_time_s;
                if (dt > 0.25 && dt < 60.0) {
                    p.vel_v_mps = (p.alt_m - g_prev_alt_m) / (float)dt;
                    p.have_vel_v = 1;
                }
            }
            g_prev_alt_m = p.alt_m;
            g_prev_alt_time_s = g_sample_seconds;
            g_have_prev_alt = 1;
        }
        if (g_json_mode) {
            char ts[64];
            iso8601_now(ts, sizeof(ts));
            uint32_t frame_number = 0;
            char idstr[32];
            char rawidstr[32];
            if (!p.have_alt && p.have_pressure && p.pressure_hpa > 1.0f) {
                p.alt_m = 44330.0f * (1.0f - powf(p.pressure_hpa / 1013.25f, 0.190263f));
                p.have_alt = 1;
            }
            if (p.have_gps && p.lat != 0 && p.lon != 0) {
                frame_number = g_frame_number++;
                if (p.have_id) {
                    snprintf(idstr, sizeof(idstr), "WS%u", p.id);
                    snprintf(rawidstr, sizeof(rawidstr), "WS_%04X", p.id);
                } else if (p.have_sid) {
                    snprintf(idstr, sizeof(idstr), "WSS%u", p.sid);
                    snprintf(rawidstr, sizeof(rawidstr), "WS_S%X", p.sid);
                } else {
                    snprintf(idstr, sizeof(idstr), "WSXXXXX");
                    snprintf(rawidstr, sizeof(rawidstr), "WS_XXXX");
                }
                printf("{ \"type\": \"S1\", \"frame\": %u, \"id\": \"%s\", \"datetime\": \"%s\"",
                    frame_number, idstr, ts);
                if (p.have_gps) {
                    printf(", \"lat\": %.5f, \"lon\": %.5f", p.lat, p.lon);
                } else {
                    printf(", \"lat\": 0.00000, \"lon\": 0.00000");
                }
                printf(", \"alt\": %.2f", p.have_alt ? p.alt_m : 0.0);
                printf(", \"vel_h\": %.5f", p.have_speed ? p.speed_kmh / 3.6 : 0.0);
                if (p.have_vel_v) printf(", \"vel_v\": %.5f", p.vel_v_mps);
                printf(", \"heading\": %.5f", p.have_speed ? p.direction_raw : 0.0);
                if (p.have_temperature) printf(", \"temp\": %.2f", p.temperature_c);
                if (p.have_humidity)    printf(", \"humidity\": %.1f", p.humidity_percent);
                if (p.have_pressure)    printf(", \"pressure\": %.2f", p.pressure_hpa);
                if (p.have_vbat)        printf(", \"batt\": %.2f", p.vbat_v);
                if (g_tx_frequency_hz > 0.0) printf(", \"tx_frequency\": %.0f", g_tx_frequency_hz);
                printf(", \"rawid\": \"%s\"", rawidstr);
                printf(", \"subtype\": \"0\"");
                printf(", \"ref_datetime\": \"UTC\", \"ref_position\": \"%s\" }\n", p.have_gps ? "GPS" : "MSL");
            }
            fflush(stdout);
        } else {
            printf("[t=%.2fs] WINDSOND_S1", g_sample_seconds);
            if (p.have_id)          printf(" id=%u", p.id);
            else if (p.have_sid)    printf(" sid=%u", p.sid);
            if (p.have_temperature) printf(" temp=%.2fC", p.temperature_c);
            if (p.have_humidity)    printf(" hum=%.1f%%", p.humidity_percent);
            if (p.have_pressure)    printf(" pressure=%.2fhPa", p.pressure_hpa);
            if (p.have_alt)         printf(" alt=%.0fm", p.alt_m);
            if (p.have_gps)         printf(" lat=%.5f lon=%.5f", p.lat, p.lon);
            if (p.have_speed)       printf(" speed=%.1fkm/h heading=%.2fdeg", p.speed_kmh, p.direction_raw);
            if (p.have_vel_v)       printf(" climb=%.2fm/s", p.vel_v_mps);
            if (p.have_vbat)        printf(" batt=%.2fV", p.vbat_v);
            if (p.have_lux)         printf(" lux_raw=%u", p.lux_raw);
            if (p.history_count)    printf(" hist=%d", p.history_count);
            printf(" golay=%d/%d", p.golay_corrected, p.golay_failed);
            printf("\n");
            fflush(stdout);
        }
    } else if ((rxdat[1] & 0x0F) == 0x0A && crc_pos == 5) {

        if (!g_json_mode) {
            printf("[t=%.2fs] WINDSOND_S1 heartbeat counter=%u\n", g_sample_seconds, rxdat[5]);
            fflush(stdout);
        } else if (g_verbose || getenv("S1_DEBUG")) {
            fprintf(stderr, "[t=%.2fs] heartbeat counter=%u\n", g_sample_seconds, rxdat[5]);
        }
    } else {
        if (!g_json_mode) {
            printf("[t=%.2fs] WINDSOND_S1 (non-meteo frame, opcode=%d, len=%d) raw=", g_sample_seconds, rxdat[1] & 0x0F, crc_pos + 3);
            for (i = 0; i < S1_DATLEN; i++) printf("%02X", rxdat[i]);
            printf("\n");
            fflush(stdout);
        } else if (g_verbose || getenv("S1_DEBUG")) {
            fprintf(stderr, "[t=%.2fs] non-meteo frame, opcode=%d, len=%d\n", g_sample_seconds, rxdat[1] & 0x0F, crc_pos + 3);
        }
    }
    return 1;
}

#define S1_SYNC32       0x5555552DUL
#define S1_SYNC32_INV   0xAAAAAAD2UL
#define S1_FRAME_BITS   (S1_FLEN * 8)
#define S1_MAX_SPS      256

typedef struct {
    double sps;
    int nphase;
    uint32_t sr[S1_MAX_SPS];
    long n;

    int run_active;
    long run_first, run_last;
    int run_inverted;

    int in_frame;
    int inverted;
    double next_t;
    int nbits;
    uint8_t cur;
    uint8_t rxbuf[S1_FLEN];
    float prev_x;
    long last_zc;
    int last_bit;

    float *box;
    int boxlen, boxpos;
    float boxsum;
} S1_Demod;

static void s1_demod_init(S1_Demod *r, double sample_rate, double baud) {
    memset(r, 0, sizeof(*r));
    r->sps = sample_rate / baud;
    r->nphase = (int)lround(r->sps);
    if (r->nphase < 4) r->nphase = 4;
    if (r->nphase > S1_MAX_SPS) r->nphase = S1_MAX_SPS;
    r->boxlen = (int)(r->sps / 2.0);
    if (r->boxlen < 1) r->boxlen = 1;
    r->box = (float *)calloc((size_t)r->boxlen, sizeof(float));
    r->last_zc = -1;
}

static float s1_box(S1_Demod *r, float in) {
    r->boxsum += in - r->box[r->boxpos];
    r->box[r->boxpos] = in;
    if (++r->boxpos >= r->boxlen) r->boxpos = 0;
    return r->boxsum / (float)r->boxlen;
}

static void s1_frame_bit(S1_Demod *r, int bit) {
    r->cur = (uint8_t)((r->cur << 1) | (bit & 1));
    r->nbits++;
    if ((r->nbits & 7) == 0) {
        int idx = (r->nbits >> 3) - 1;
        r->rxbuf[idx] = (uint8_t)(r->cur ^ s1_whitening[idx % 64]);
        if (idx + 1 >= S1_FLEN) {
            s1_process_frame(r->rxbuf);
            r->in_frame = 0;
            memset(r->sr, 0, sizeof(r->sr));
        }
    }
}

static void s1_demod_sample(S1_Demod *r, float raw) {
    float x = s1_box(r, raw);
    long n = r->n++;
    int s = (x >= 0.0f);

    if ((x >= 0.0f) != (r->prev_x >= 0.0f)) r->last_zc = n;
    r->prev_x = x;

    if (r->in_frame) {
        if ((double)n + 0.5 >= r->next_t) {
            int bit = s ^ r->inverted;
            if (bit != r->last_bit && r->last_zc >= 0) {
                double expected = r->next_t - r->sps / 2.0;
                double err = (double)r->last_zc - expected;
                if (fabs(err) < r->sps / 2.0) r->next_t += 0.1 * err;
            }
            r->last_bit = bit;
            r->next_t += r->sps;
            s1_frame_bit(r, bit);
        }
        return;
    }

    int ph = (int)(n % r->nphase);
    r->sr[ph] = (r->sr[ph] << 1) | (uint32_t)s;
    uint32_t w = r->sr[ph];
    int match = (w == S1_SYNC32) ? 1 : (w == S1_SYNC32_INV) ? 2 : 0;
    if (match) {
        if (!r->run_active) {
            r->run_active = 1;
            r->run_first = n;
            r->run_inverted = (match == 2);
        }
        r->run_last = n;
    } else if (r->run_active && n - r->run_last > 1) {
        double centre = 0.5 * (double)(r->run_first + r->run_last);
        r->run_active = 0;
        r->in_frame = 1;
        r->inverted = r->run_inverted;
        r->next_t = centre + r->sps;
        r->nbits = 0;
        r->cur = 0;
        r->last_bit = 1;
        if (g_verbose || getenv("S1_DEBUG")) {
            fprintf(stderr, "[t=%.2fs] sync%s phases=%ld\n", g_sample_seconds,
                    r->inverted ? " (inverted)" : "", r->run_last - r->run_first + 1);
        }
    }
}

static void print_usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [--samplerate HZ] [--iq] [--verbose] [--baud N] [--json] [--frequency HZ]\n"
        "  --samplerate HZ   input sample rate (default 48000, use >= 96000 for IQ)\n"
        "  --iq              input is interleaved I/Q int16 pairs instead\n"
        "                    of mono FM-discriminated audio\n"
        "  --baud N          assumed baud rate (default 2400)\n"
        "  --json            emit one wsrx-style JSON object per meteo frame\n"
        "                    on stdout, instead of the human-readable text\n"
        "  --frequency HZ    tx_frequency to embed in JSON output (optional)\n"
        "  --verbose         print debug output on stderr\n"
        "  --lowpass HZ      ignored (kept for compatibility)\n"
        "Reads 16-bit signed samples from stdin, writes decoded frames to\n"
        "stdout.\n",
        argv0);
}

int main(int argc, char **argv) {
    double sample_rate = 48000.0;
    double baud = S1_BAUD;
    int iq_mode = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--samplerate") == 0 && i + 1 < argc) {
            sample_rate = atof(argv[++i]);
        } else if (strcmp(argv[i], "--lowpass") == 0 && i + 1 < argc) {
            ++i;
        } else if (strcmp(argv[i], "--baud") == 0 && i + 1 < argc) {
            baud = atof(argv[++i]);
        } else if (strcmp(argv[i], "--iq") == 0) {
            iq_mode = 1;
        } else if (strcmp(argv[i], "--json") == 0) {
            g_json_mode = 1;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            g_verbose = 1;
        } else if (strcmp(argv[i], "--frequency") == 0 && i + 1 < argc) {
            g_tx_frequency_hz = atof(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    if (sample_rate / baud > S1_MAX_SPS) {
        fprintf(stderr, "samplerate too high for baud rate (max %d samples/bit)\n", S1_MAX_SPS);
        return 1;
    }
    S1_Demod rx;
    s1_demod_init(&rx, sample_rate, baud);
    fprintf(stderr, "windsondmod: samplerate=%.0f iq=%d sps=%.2f\n",
            sample_rate, iq_mode, rx.sps);

    if (!iq_mode) {
        int16_t s;
        while (fread(&s, sizeof(s), 1, stdin) == 1) {
            g_sample_seconds += 1.0 / sample_rate;
            s1_demod_sample(&rx, (float)s / 32768.0f);
        }
    } else {
        int16_t iq[2];
        float i_prev = 0.0f, q_prev = 0.0f;
        while (fread(iq, sizeof(int16_t), 2, stdin) == 2) {
            g_sample_seconds += 1.0 / sample_rate;
            float i_s = iq[0], q_s = iq[1];
            float re = i_s * i_prev + q_s * q_prev;
            float im = q_s * i_prev - i_s * q_prev;
            i_prev = i_s;
            q_prev = q_s;
            s1_demod_sample(&rx, atan2f(im, re));
        }
    }
    free(rx.box);
    return 0;
}