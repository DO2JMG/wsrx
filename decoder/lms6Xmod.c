

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef CYGWIN
  #include <fcntl.h>
  #include <io.h>
#endif

#ifdef VERSION_JSN
  #include "version_jsn.h"
#endif



#include "demod_mod.h"

#ifdef INCLUDESTATIC
    #include "bch_ecc_mod.c"
#else
    #include "bch_ecc_mod.h"
#endif


typedef struct {
    i8_t vbs;
    i8_t raw;
    i8_t crc;
    i8_t ecc;
    i8_t sat;
    i8_t ptu;
    i8_t inv;
    i8_t vit;
    i8_t jsn;
} option_t;



#define BAUD_RATE6   (4800.0)
#define BAUD_RATEX   (4797.8)

#define BITS 8
#define HEADOFS  0
#define HEADLEN ((4*16)-HEADOFS)

#define SYNC_LEN 5
#define FRM_LEN    (223)
#define PAR_LEN    (32)
#define FRMBUF_LEN (3*FRM_LEN)
#define BLOCKSTART (SYNC_LEN*BITS*2)
#define BLOCK_LEN  (FRM_LEN+PAR_LEN+SYNC_LEN)
#define RAWBITBLOCK_LEN   (300*BITS*2)
#define RAWBITBLOCK_LEN_6 ((BLOCK_LEN+1)*BITS*2)

#define FRAME_LEN       (300)
#define BITFRAME_LEN    (FRAME_LEN*BITS)
#define RAWBITFRAME_LEN (BITFRAME_LEN*2)
#define OVERLAP 64
#define OFS 4


static char  rawheader[] = "0101011000001000""0001110010010111""0001101010100111""0011110100111110";
static ui8_t rs_sync[] = { 0x00, 0x58, 0xf3, 0x3f, 0xb8};

static char  blk_syncbits[] = "0000000000000000""0000001101011101""0100100111000010""0100111111110010""0110100001101011";

static ui8_t frm_sync6[] = { 0x24, 0x54, 0x00, 0x00};
static ui8_t frm_syncX[] = { 0x24, 0x46, 0x05, 0x00};


#define L 7
static char polyA[] = "1001111";
static char polyB[] = "1101101";

#define N (1 << L)
#define M (1 << (L-1))

typedef struct {
    ui8_t bIn;
    ui8_t codeIn;
    ui8_t prevState;
    float w;
} states_t;

typedef struct {
    hsbit_t  rawbits[RAWBITFRAME_LEN+OVERLAP*BITS*2 +8];
    states_t state[RAWBITFRAME_LEN+OVERLAP +8][M];
    states_t d[N];
} VIT_t;

typedef struct {
    int frnr;
    int sn;
    int week;
    int gpstow; int gpssec;
    double gpstowX;
    int jahr; int monat; int tag;
    int wday;
    int std; int min; float sek;
    double lat; double lon; double alt;
    double vH; double vD; double vV;
    double vE; double vN; double vU;
    hsbit_t  blk_rawbits[RAWBITBLOCK_LEN+SYNC_LEN*BITS*2 +9];
    ui8_t frame[FRM_LEN];
    int frm_pos;
    int sf6;
    int sfX;
    int typ;
    int jsn_freq;
    float frm_rate;
    int auto_detect;
    int reset_dsp;
    option_t option;
    RS_t RS;
    VIT_t *vit;
} gpx_t;


static int gpstow_start = -1;
static double time_elapsed_sec = 0.0;

static void Gps2Date(gpx_t *gpx) {
    long GpsDays, Mjd;
    long _J, _C, _Y, _M;

    GpsDays = gpx->week * 7 + (gpx->gpssec / 86400);
    Mjd = 44244 + GpsDays;

    _J = Mjd + 2468570;
    _C = 4 * _J / 146097;
    _J = _J - (146097 * _C + 3) / 4;
    _Y = 4000 * (_J + 1) / 1461001;
    _J = _J - 1461 * _Y / 4 + 31;
    _M = 80 * _J / 2447;
    gpx->tag = _J - 2447 * _M / 80;
    _J = _M / 11;
    gpx->monat = _M + 2 - (12 * _J);
    gpx->jahr = 100 * (_C - 49) + _Y + _J;
}


static ui8_t vit_code[N];
static int vitCodes_init = 0;

static int vit_initCodes(gpx_t *gpx) {
    int cA, cB;
    int i, bits;

    VIT_t *pv = calloc(1, sizeof(VIT_t));
    if (pv == NULL) return -1;
    gpx->vit = pv;

    if ( vitCodes_init == 0 ) {
        for (bits = 0; bits < N; bits++) {
            cA = 0;
            cB = 0;
            for (i = 0; i < L; i++) {
                cA ^= (polyA[L-1-i]&1) & ((bits >> i)&1);
                cB ^= (polyB[L-1-i]&1) & ((bits >> i)&1);
            }
            vit_code[bits] = (cA<<1) | cB;
        }
        vitCodes_init = 1;
    }

    return 0;
}

static float vit_dist2(int c, hsbit_t *rc) {
    int c0 = 2*((c>>1) & 1)-1;
    int c1 = 2*(c & 1)-1;
    float d2 = (c0-rc[0].sb)*(c0-rc[0].sb) + (c1-rc[1].sb)*(c1-rc[1].sb);
    return d2;
}

static int vit_start(VIT_t *vit, hsbit_t *rc) {
    int t, m, j, c;
    float d;

    t = L-1;
    m = M;
    while ( t > 0 ) {
        for (j = 0; j < m; j++) {
            vit->state[t][j].prevState = j/2;
        }
        t--;
        m /= 2;
    }

    m = 2;
    for (t = 1; t < L; t++) {
        for (j = 0; j < m; j++) {
            c = vit_code[j];
            vit->state[t][j].bIn = j % 2;
            vit->state[t][j].codeIn = c;
            d = vit_dist2( c, rc+2*(t-1) );
            vit->state[t][j].w = vit->state[t-1][vit->state[t][j].prevState].w + d;
        }
        m *= 2;
    }

    return t;
}

static int vit_next(VIT_t *vit, int t, hsbit_t *rc) {
    int b, nstate;
    int j, index;

    for (j = 0; j < M; j++) {
        for (b = 0; b < 2; b++) {
            nstate = j*2 + b;
            vit->d[nstate].bIn = b;
            vit->d[nstate].codeIn = vit_code[nstate];
            vit->d[nstate].prevState = j;
            vit->d[nstate].w = vit->state[t][j].w + vit_dist2( vit->d[nstate].codeIn, rc );
        }
     }

    for (j = 0; j < M; j++) {

        if ( vit->d[j].w <= vit->d[j+M].w ) index = j; else index = j+M;

        vit->state[t+1][j] = vit->d[index];
    }

    return 0;
}

static int vit_path(VIT_t *vit, int j, int t) {
    int c;

    vit->rawbits[2*t].hb = '\0';
    while (t > 0) {
        c = vit->state[t][j].codeIn;
        vit->rawbits[2*t -2].hb = 0x30 + ((c>>1) & 1);
        vit->rawbits[2*t -1].hb = 0x30 + (c & 1);
        j = vit->state[t][j].prevState;
        t--;
    }

    return 0;
}

static int hbstr_len(hsbit_t *hsbit) {
    int len = 0;
    while (hsbit[len].hb) len++;
    return len;
}

static int viterbi(VIT_t *vit, hsbit_t *rc) {
    int t, tmax;
    int j, j_min;
    float w_min;

    vit_start(vit, rc);

    tmax = hbstr_len(rc)/2;

    for (t = L-1; t < tmax; t++)
    {
        vit_next(vit, t, rc+2*t);
    }

    w_min = -1;
    for (j = 0; j < M; j++) {
        if (w_min < 0) {
            w_min = vit->state[tmax][j].w;
            j_min = j;
        }
        if (vit->state[tmax][j].w < w_min) {
            w_min = vit->state[tmax][j].w;
            j_min = j;
        }
    }
    vit_path(vit, j_min, tmax);

    return 0;
}


static int deconv(hsbit_t *rawbits, char *bits) {

    int j, n, bitA, bitB;
    hsbit_t *p;
    int len;
    int errors = 0;
    int m = L-1;

    len = hbstr_len(rawbits);
    for (j = 0; j < m; j++) bits[j] = '0';
    n = 0;
    while ( 2*(m+n) < len ) {
        p = rawbits+2*(m+n);
        bitA = bitB = 0;
        for (j = 0; j < m; j++) {
            bitA ^= (bits[n+j]&1) & (polyA[j]&1);
            bitB ^= (bits[n+j]&1) & (polyB[j]&1);
        }
        if      ( (bitA^(p[0].hb&1))==(polyA[m]&1)  &&  (bitB^(p[1].hb&1))==(polyB[m]&1) ) bits[n+m] = '1';
        else if ( (bitA^(p[0].hb&1))==0             &&  (bitB^(p[1].hb&1))==0            ) bits[n+m] = '0';
        else {
            if ( (bitA^(p[0].hb&1))!=(polyA[m]&1) && (bitB^(p[1].hb&1))==(polyB[m]&1) ) bits[n+m] = 0x39;
            else bits[n+m] = 0x38;
            errors = n;
            break;
        }
        n += 1;
    }
    bits[n+m] = '\0';

    return errors;
}


static int crc16_0(ui8_t frame[], int len) {
    int crc16poly = 0x1021;
    int rem = 0x0, i, j;
    int byte;

    for (i = 0; i < len; i++) {
        byte = frame[i];
        rem = rem ^ (byte << 8);
        for (j = 0; j < 8; j++) {
            if (rem & 0x8000) {
                rem = (rem << 1) ^ crc16poly;
            }
            else {
                rem = (rem << 1);
            }
            rem &= 0xFFFF;
        }
    }
    return rem;
}

static int check_CRC(ui8_t frame[]) {
    ui32_t crclen = 0,
           crcdat = 0;

    crclen = 221;
    crcdat = (frame[crclen]<<8) | frame[crclen+1];
    if ( crcdat != crc16_0(frame, crclen) ) {
        return 1;
    }
    else return 0;
}


static int bits2bytes(char *bitstr, ui8_t *bytes) {
    int i, bit, d, byteval;
    int len = strlen(bitstr)/8;
    int bitpos, bytepos;

    bitpos = 0;
    bytepos = 0;

    while (bytepos < len) {

        byteval = 0;
        d = 1;
        for (i = 0; i < BITS; i++) {
            bit=*(bitstr+bitpos+i);
            if        ((bit == '1') || (bit == '9'))    byteval += d;
            else    byteval += 0;
            d <<= 1;
        }
        bitpos += BITS;
        bytes[bytepos++] = byteval & 0xFF;
    }


    return bytepos;
}



#define pos_SondeSN  (OFS+0x00)
#define pos_FrameNb  (OFS+0x04)
#define pos_GPSTOW   (OFS+0x06)
#define pos_GPSlat   (OFS+0x0E)
#define pos_GPSlon   (OFS+0x12)
#define pos_GPSalt   (OFS+0x16)
#define pos_GPSvE    (OFS+0x1A)
#define pos_GPSvN    (OFS+0x1D)
#define pos_GPSvU    (OFS+0x20)
#define pos_GPSvH    (OFS+0x1A)
#define pos_GPSvD    (OFS+0x1C)
#define pos_GPSvV    (OFS+0x1E)


static int get_SondeSN(gpx_t *gpx) {
    unsigned byte;

    byte =  (gpx->frame[pos_SondeSN]<<24) | (gpx->frame[pos_SondeSN+1]<<16)
          | (gpx->frame[pos_SondeSN+2]<<8) | gpx->frame[pos_SondeSN+3];
    gpx->sn = byte & 0xFFFFFF;

    return 0;
}

static int get_FrameNb(gpx_t *gpx) {
    ui8_t *frnr_bytes;
    int frnr;

    frnr_bytes = gpx->frame+pos_FrameNb;

    frnr = (frnr_bytes[0] << 8) + frnr_bytes[1] ;
    gpx->frnr = frnr;

    return 0;
}


static char weekday[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static int get_GPStime(gpx_t *gpx, int crc_err) {
    int i;
    ui8_t *gpstime_bytes;
    int gpstime = 0,
        day;
    float ms;

    gpstime_bytes = gpx->frame+pos_GPSTOW;

    gpstime = 0;
    for (i = 0; i < 4; i++) {
        gpstime |= gpstime_bytes[i] << (8*(3-i));
    }

    if (gpstow_start < 0 && !crc_err) {
        gpstow_start = gpstime;
        if (gpx->week > 0 && gpstime/1000.0 < time_elapsed_sec) gpx->week += 1;
    }
    gpx->gpstow = gpstime;

    ms = gpstime % 1000;
    gpstime /= 1000;
    gpx->gpssec = gpstime;

    day = gpstime / (24 * 3600);
    gpstime %= (24*3600);

    if ((day < 0) || (day > 6)) return -1;

    gpx->wday = day;
    gpx->std = gpstime / 3600;
    gpx->min = (gpstime % 3600) / 60;
    gpx->sek = gpstime % 60 + ms/1000.0;

    return 0;
}

static int get_GPStime_X(gpx_t *gpx) {
    int i;
    unsigned byte;
    ui32_t gpstime, tow_u4;
    int day;
    float ms;
    ui32_t w[2];
    double *f64 = (double*)w;

    w[0] = 0;
    for (i = 0; i < 4; i++) {
        byte = gpx->frame[pos_GPSTOW + i];
        w[0] |= byte << (8*(3-i));
    }
    w[1] = 0;
    for (i = 0; i < 4; i++) {
        byte = gpx->frame[pos_GPSTOW+4 + i];
        w[1] |= byte << (8*(3-i));
    }

    gpx->gpstowX = *f64;
    gpx->gpstow = (ui32_t)(gpx->gpstowX*1e3);
    tow_u4 = (ui32_t)gpx->gpstowX;
    gpstime = tow_u4;
    gpx->gpssec = tow_u4;

    day = gpstime / (24 * 3600);
    gpstime %= (24*3600);

    if ((day < 0) || (day > 6)) return -1;

    gpx->wday = day;
    gpx->std = gpstime / 3600;
    gpx->min = (gpstime % 3600) / 60;
    gpx->sek = (gpstime % 60) + *f64 - tow_u4;

    return 0;
}

static double B60B60 = (1<<30)/90.0;

static int get_GPSlat(gpx_t *gpx) {
    int i;
    ui8_t *gpslat_bytes;
    int gpslat;
    double lat;

    gpslat_bytes = gpx->frame+pos_GPSlat;

    gpslat = 0;
    for (i = 0; i < 4; i++) {
        gpslat |= gpslat_bytes[i] << (8*(3-i));
    }
    if ((gpx->typ & 0xFF) == 6) lat = gpslat / B60B60;
    else     lat = gpslat / 1e7;

    gpx->lat = lat;

    return 0;
}

static int get_GPSlon(gpx_t *gpx) {
    int i;
    ui8_t *gpslon_bytes;
    int gpslon;
    double lon;

    gpslon_bytes = gpx->frame+pos_GPSlon;

    gpslon = 0;
    for (i = 0; i < 4; i++) {
        gpslon |= gpslon_bytes[i] << (8*(3-i));
    }

    if ((gpx->typ & 0xFF) == 6) lon = gpslon / B60B60;
    else     lon = gpslon / 1e7;

    gpx->lon = lon;

    return 0;
}

static int get_GPSalt(gpx_t *gpx) {
    int i;
    ui8_t *gpsheight_bytes;
    int gpsheight;
    double alt;

    gpsheight_bytes = gpx->frame+pos_GPSalt;

    gpsheight = 0;
    for (i = 0; i < 4; i++) {
        gpsheight |= gpsheight_bytes[i] << (8*(3-i));
    }

    if ((gpx->typ & 0xFF) == 6) alt = gpsheight / 1000.0;
    else     alt = gpsheight / 100.0;

    gpx->alt = alt;

    if (alt < -200 || alt > 60000) return -1;
    return 0;
}

static int get_GPSvel24(gpx_t *gpx) {
    ui8_t *gpsVel_bytes;
    int vel24;
    double vx, vy, vz, dir;

    gpsVel_bytes = gpx->frame+pos_GPSvE;
    vel24 = gpsVel_bytes[0] << 16 | gpsVel_bytes[1] << 8 | gpsVel_bytes[2];
    if (vel24 > (0x7FFFFF)) vel24 -= 0x1000000;
    vx = vel24 / 1e3;

    gpsVel_bytes = gpx->frame+pos_GPSvN;
    vel24 = gpsVel_bytes[0] << 16 | gpsVel_bytes[1] << 8 | gpsVel_bytes[2];
    if (vel24 > (0x7FFFFF)) vel24 -= 0x1000000;
    vy = vel24 / 1e3;

    gpsVel_bytes = gpx->frame+pos_GPSvU;
    vel24 = gpsVel_bytes[0] << 16 | gpsVel_bytes[1] << 8 | gpsVel_bytes[2];
    if (vel24 > (0x7FFFFF)) vel24 -= 0x1000000;
    vz = vel24 / 1e3;

    gpx->vE = vx;
    gpx->vN = vy;
    gpx->vU = vz;


    gpx->vH = sqrt(vx*vx+vy*vy);
    dir = atan2(vx, vy) * 180 / M_PI;
    if (dir < 0) dir += 360;
    gpx->vD = dir;

    gpx->vV = vz;

    return 0;
}

static int get_GPSvel16_X(gpx_t *gpx) {
    ui8_t *gpsVel_bytes;
    short vel16;
    double vx, vy, vz;

    gpsVel_bytes = gpx->frame+pos_GPSvH;
    vel16 = gpsVel_bytes[0] << 8 | gpsVel_bytes[1];
    vx = vel16 / 1e2;

    gpsVel_bytes = gpx->frame+pos_GPSvD;
    vel16 = gpsVel_bytes[0] << 8 | gpsVel_bytes[1];
    vy = vel16 / 1e2;

    gpsVel_bytes = gpx->frame+pos_GPSvV;
    vel16 = gpsVel_bytes[0] << 8 | gpsVel_bytes[1];
    vz = vel16 / 1e2;

    gpx->vH = vx;
    gpx->vD = vy;
    gpx->vV = vz;

    return 0;
}


#define rs_N 255
#define rs_K 223
#define rs_R (rs_N-rs_K)

static int lms6_ecc(gpx_t *gpx, ui8_t *cw) {
    int errors;
    ui8_t err_pos[rs_R],
          err_val[rs_R];

    errors = rs_decode(&gpx->RS, cw, err_pos, err_val);

    return errors;
}

static void print_frame(gpx_t *gpx, int crc_err, int len) {
    int err1=0, err2=0;

    if (gpx->frame[0] != 0)
    {
        if ( gpx->frame[pos_SondeSN+1] )
        {
            get_SondeSN(gpx);
            get_FrameNb(gpx);
            printf(" (%7d) ", gpx->sn);
            printf(" [%5d] ", gpx->frnr);

            get_GPSlat(gpx);
            get_GPSlon(gpx);
            err2 = get_GPSalt(gpx);
            if ((gpx->typ & 0xFF) == 6)
            {
                err1 = get_GPStime(gpx, crc_err);
                get_GPSvel24(gpx);
            }
            else {
                err1 = get_GPStime_X(gpx);
                get_GPSvel16_X(gpx);
            }

            if (!err1) printf("%s ", weekday[gpx->wday]);
            if (gpx->week > 0) {
                if (gpx->gpstow < gpstow_start && !crc_err) {
                    gpx->week += 1;
                    gpstow_start = gpx->gpstow;
                }
                Gps2Date(gpx);
                fprintf(stdout, "%04d-%02d-%02d ", gpx->jahr, gpx->monat, gpx->tag);
            }
            printf("%02d:%02d:%06.3f ", gpx->std, gpx->min, gpx->sek);

            if (!err2) {
                printf(" lat: %.5f ", gpx->lat);
                printf(" lon: %.5f ", gpx->lon);
                printf(" alt: %.2fm ", gpx->alt);
                printf("  vH: %.1fm/s  D: %.1f  vV: %.1fm/s ", gpx->vH, gpx->vD, gpx->vV);
            }

            if (crc_err==0) printf(" [OK]"); else printf(" [NO]");

            printf("\n");


            if (gpx->option.jsn) {
                if (crc_err==0) {
                    char *ver_jsn = NULL;
                    char sntyp[]  = "LMS6-";
                    char subtyp[] = "LMS6-403\0\0";
                    if (gpx->typ == 10) { sntyp[3] = 'X'; subtyp[3] = 'X'; }
                    else if (gpx->typ == 0x0206) strcpy(subtyp, "LMS6-403-2");
                    printf("{ \"type\": \"%s\"", "LMS");
                    printf(", \"frame\": %d, \"id\": \"%s%d\", \"datetime\": \"", gpx->frnr, sntyp, gpx->sn );
                    printf("%02d:%02d:%06.3fZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f, \"vel_h\": %.5f, \"heading\": %.5f, \"vel_v\": %.5f",
                           gpx->std, gpx->min, gpx->sek, gpx->lat, gpx->lon, gpx->alt, gpx->vH, gpx->vD, gpx->vV );
                    printf(", \"gpstow\": %d", gpx->gpstow );
                    printf(", \"subtype\": \"%s\"", subtyp);
                    if (gpx->jsn_freq > 0) {
                        printf(", \"freq\": %d", gpx->jsn_freq);
                    }

                    printf(", \"ref_datetime\": \"%s\"", "GPS" );
                    printf(", \"ref_position\": \"%s\"", "GPS" );

                    #ifdef VER_JSN_STR
                        ver_jsn = VER_JSN_STR;
                    #endif
                    if (ver_jsn && *ver_jsn != '\0') printf(", \"version\": \"%s\"", ver_jsn);
                    printf(" }\n");
                    printf("\n");
                }
            }

        }
    }
}

static int frmsync_6(gpx_t *gpx, ui8_t block_bytes[], int blk_pos) {
    int j;

    while ( blk_pos-SYNC_LEN < FRM_LEN ) {
        int sf6_00 = 0;
        int sf6_05 = 0;
        gpx->sf6 = 0;
        for (j = 0; j < 3; j++) gpx->sf6 += (block_bytes[blk_pos+j] == frm_sync6[j]);
        sf6_00 = gpx->sf6 + (block_bytes[blk_pos+3] == 0x00);
        sf6_05 = gpx->sf6 + (block_bytes[blk_pos+3] == 0x05);
        if (sf6_00 == 4 || sf6_05 == 4)  {
            gpx->sf6 = 4;
            gpx->frm_pos = 0;
            gpx->typ = 6;
            if (sf6_05 == 4) gpx->typ |= 0x0200;
            break;
        }
        blk_pos++;
    }

    return blk_pos;
}

static int frmsync_X(gpx_t *gpx, ui8_t block_bytes[]) {
    int j;
    int blk_pos = SYNC_LEN;

    gpx->sfX = 0;
    for (j = 0; j < 4; j++) gpx->sfX += (block_bytes[SYNC_LEN+j] == frm_syncX[j]);
    if (gpx->sfX < 4) {
        gpx->sfX = 0;
        for (j = 0; j < 4; j++) gpx->sfX += (block_bytes[SYNC_LEN+35+j] == frm_syncX[j]);
        if (gpx->sfX == 4)  blk_pos = SYNC_LEN+35;
        else {
            gpx->sfX = 0;
            for (j = 0; j < 4; j++) gpx->sfX += (block_bytes[SYNC_LEN+40+j] == frm_syncX[j]);
            if (gpx->sfX == 4)  blk_pos = SYNC_LEN+40;
        }
    }

    return blk_pos;
}

static void proc_frame(gpx_t *gpx, int len) {
    int blk_pos = SYNC_LEN;
    ui8_t block_bytes[FRAME_LEN+8];
    ui8_t rs_cw[rs_N];
    char  frame_bits[BITFRAME_LEN+OVERLAP*BITS +8];
    hsbit_t *rawbits = NULL;
    int i, j;
    int err = 0;
    int errs = 0;
    int crc_err = 0;
    int flen, blen;


    if ((len % 8) > 4) {
        while (len % 8) {
            gpx->blk_rawbits[len].hb = '0';
            gpx->blk_rawbits[len].sb = -1;
            len++;
        }
    }
    gpx->blk_rawbits[len].hb = '\0';

    flen = len / (2*BITS);

    if (gpx->option.vit) {
        viterbi(gpx->vit, gpx->blk_rawbits);
        rawbits = gpx->vit->rawbits;
    }
    else rawbits = gpx->blk_rawbits;

    err = deconv(rawbits, frame_bits);

    if (err) { for (i=err; i < RAWBITBLOCK_LEN/2; i++) frame_bits[i] = 0; }


    blen = bits2bytes(frame_bits, block_bytes);
    for (j = blen; j < FRAME_LEN+8; j++) block_bytes[j] = 0;


    blk_pos = SYNC_LEN;

    if ((gpx->typ & 0xFF) == 6)
    {
        if (gpx->option.ecc) {
            for (j = 0; j < rs_N; j++) rs_cw[rs_N-1-j] = block_bytes[SYNC_LEN+j];
            errs = lms6_ecc(gpx, rs_cw);
            for (j = 0; j < rs_N; j++) block_bytes[SYNC_LEN+j] = rs_cw[rs_N-1-j];
        }

        while ( blk_pos-SYNC_LEN < FRM_LEN ) {

            if (gpx->sf6 == 0)
            {
                blk_pos = frmsync_6(gpx, block_bytes, blk_pos);

                if (gpx->sf6 < 4) {
                    frmsync_X(gpx, block_bytes);
                    if (gpx->sfX == 4)  {
                        if (gpx->auto_detect) { gpx->typ = 10; gpx->reset_dsp = 1; }
                        break;
                    }
                }
            }

            if ( gpx->sf6  &&  gpx->frm_pos < FRM_LEN ) {
                gpx->frame[gpx->frm_pos] = block_bytes[blk_pos];
                gpx->frm_pos++;
                blk_pos++;
            }

            if (gpx->frm_pos == FRM_LEN) {

                crc_err = check_CRC(gpx->frame);

                if (gpx->option.raw == 1) {
                    for (i = 0; i < FRM_LEN; i++) printf("%02x ", gpx->frame[i]);
                    if (crc_err==0) printf(" [OK]"); else printf(" [NO]");
                    printf("\n");
                }

                if (gpx->option.raw == 0) print_frame(gpx, crc_err, len);

                gpx->frm_pos = 0;
                gpx->sf6 = 0;
            }
        }
    }

    if (gpx->typ == 10)
    {
        blk_pos = frmsync_X(gpx, block_bytes);

        if (gpx->sfX < 4) {
            while ( blk_pos-SYNC_LEN < FRM_LEN ) {
                int sf6_00 = 0;
                int sf6_05 = 0;
                gpx->sf6 = 0;
                for (j = 0; j < 3; j++) gpx->sf6 += (block_bytes[blk_pos+j] == frm_sync6[j]);
                sf6_00 = gpx->sf6 + (block_bytes[blk_pos+3] == 0x00);
                sf6_05 = gpx->sf6 + (block_bytes[blk_pos+3] == 0x05);
                if (sf6_00 == 4 || sf6_05 == 4)  {
                    gpx->sf6 = 4;
                    gpx->frm_pos = 0;
                    if (gpx->auto_detect) {
                        gpx->reset_dsp = 1;
                        gpx->typ = 6;
                        if (sf6_05 == 4) gpx->typ |= 0x0200;
                    }
                    break;
                }
                blk_pos++;
            }

            if (gpx->frm_rate > 5000.0 || gpx->frm_rate < 4000.0) {
                if (gpx->auto_detect) {
                    gpx->reset_dsp = 1;
                    gpx->typ = 6;
                }
            }
        }
        else
        {
            if (blen > 100 && gpx->option.ecc) {
                for (j = 0; j < rs_N; j++) rs_cw[rs_N-1-j] = block_bytes[blk_pos+j];
                errs = lms6_ecc(gpx, rs_cw);
                for (j = 0; j < rs_N; j++) block_bytes[blk_pos+j] = rs_cw[rs_N-1-j];
            }

            for (j = 0; j < rs_K; j++) gpx->frame[j] = block_bytes[blk_pos+j];

            crc_err = check_CRC(gpx->frame);

            if (gpx->option.raw == 1) {
                for (i = 0; i < FRM_LEN; i++) printf("%02x ", gpx->frame[i]);
                if (crc_err==0) printf(" [OK]"); else printf(" [NO]");
                printf("\n");
            }

            if (gpx->option.raw == 0) print_frame(gpx, crc_err, len);
        }
    }

}



int main(int argc, char **argv) {

    int option_inv = 0;
    int option_min = 0;
    int option_iq = 0;
    int option_iqdc = 0;
    int option_lp = 0;
    int option_dc = 0;
    int option_noLUT = 0;
    int option_softin = 0;
    int option_pcmraw = 0;
    int wavloaded = 0;
    int sel_wavch = 0;
    int gpsweek = 0;
    int cfreq = -1;

    FILE *fp = NULL;
    char *fpname = NULL;

    int k;

    hsbit_t hsbit, rhsbit, rhsbit1;
    int bitpos = 0;
    int bitQ;
    int pos;

    int header_found = 0;

    float thres = 0.65;
    float _mv = 0.0;

    float lpIQ_bw = 16e3;

    int symlen = 1;
    int bitofs = 0;
    int shift = 0;

    int bitofs6 = 0;
    int bitofsX = 0;

    unsigned int bc = 0;

    ui32_t rawbitblock_len = RAWBITBLOCK_LEN_6;
    ui32_t mpos0 = 0;


    pcm_t pcm = {0};
    dsp_t dsp = {0};

    hdb_t hdb = {0};

    gpx_t _gpx = {0}; gpx_t *gpx = &_gpx;

    gpx->auto_detect = 1;
    gpx->reset_dsp = 0;


#ifdef CYGWIN
    _setmode(fileno(stdin), _O_BINARY);
#endif
    setbuf(stdout, NULL);


    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -v, --verbose\n");
            fprintf(stderr, "       -r, --raw\n");
            fprintf(stderr, "       --vit        (Viterbi)\n");
            fprintf(stderr, "       --ecc        (Reed-Solomon)\n");
            return 0;
        }
        else if   (strcmp(*argv, "--lms6" ) == 0) {
            gpx->typ = 6;
            gpx->auto_detect = 0;
        }
        else if   (strcmp(*argv, "--lmsX" ) == 0) {
            gpx->typ = 10;
            gpx->auto_detect = 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            gpx->option.vbs = 1;
        }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            gpx->option.raw = 1;
        }
        else if   (strcmp(*argv, "--ecc" ) == 0) { gpx->option.ecc = 1; }
        else if   (strcmp(*argv, "--ecc3") == 0) { gpx->option.ecc = 3; }
        else if   (strcmp(*argv, "--vit"  ) == 0) { gpx->option.vit = 1; }
        else if   (strcmp(*argv, "--vit2" ) == 0) { gpx->option.vit = 2; }
        else if ( (strcmp(*argv, "--gpsweek") == 0) ) {
            ++argv;
            if (*argv) {
                gpsweek = atoi(*argv);
                if (gpsweek < 1024 || gpsweek > 3072) gpsweek =  0;
            }
            else return -1;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            option_inv = 1;
        }
        else if   (strcmp(*argv, "--ch2") == 0) { sel_wavch = 1; }
        else if   (strcmp(*argv, "--softin") == 0)  { option_softin = 1; }
        else if   (strcmp(*argv, "--softinv") == 0) { option_softin = 2; }
        else if   (strcmp(*argv, "--ths") == 0) {
            ++argv;
            if (*argv) {
                thres = atof(*argv);
            }
            else return -1;
        }
        else if ( (strcmp(*argv, "-d") == 0) ) {
            ++argv;
            if (*argv) {
                shift = atoi(*argv);
                if (shift >  4) shift =  4;
                if (shift < -4) shift = -4;
            }
            else return -1;
        }
        else if   (strcmp(*argv, "--iq0") == 0) { option_iq = 1; }
        else if   (strcmp(*argv, "--iq2") == 0) { option_iq = 2; }
        else if   (strcmp(*argv, "--iq3") == 0) { option_iq = 3; }
        else if   (strcmp(*argv, "--iqdc") == 0) { option_iqdc = 1; }
        else if   (strcmp(*argv, "--IQ") == 0) {
            double fq = 0.0;
            ++argv;
            if (*argv) fq = atof(*argv);
            else return -1;
            if (fq < -0.5) fq = -0.5;
            if (fq >  0.5) fq =  0.5;
            dsp.xlt_fq = -fq;
            option_iq = 5;
        }
        else if   (strcmp(*argv, "--lpIQ") == 0) { option_lp |= LP_IQ; }
        else if   (strcmp(*argv, "--lpbw") == 0) {
            double bw = 0.0;
            ++argv;
            if (*argv) bw = atof(*argv);
            else return -1;
            if (bw > 4.6 && bw < 24.0) lpIQ_bw = bw*1e3;
            option_lp |= LP_IQ;
        }
        else if   (strcmp(*argv, "--lpFM") == 0) { option_lp |= LP_FM; }
        else if   (strcmp(*argv, "--dc") == 0) { option_dc = 1; }
        else if   (strcmp(*argv, "--noLUT") == 0) { option_noLUT = 1; }
        else if   (strcmp(*argv, "--min") == 0) {
            option_min = 1;
        }
        else if   (strcmp(*argv, "--json") == 0) {
            gpx->option.jsn = 1;
            gpx->option.ecc = 1;
            gpx->option.vit = 1;
        }
        else if   (strcmp(*argv, "--jsn_cfq") == 0) {
            int frq = -1;
            ++argv;
            if (*argv) frq = atoi(*argv); else return -1;
            if (frq < 300000000) frq = -1;
            cfreq = frq;
        }
        else if (strcmp(*argv, "-") == 0) {
            int sample_rate = 0, bits_sample = 0, channels = 0;
            ++argv;
            if (*argv) sample_rate = atoi(*argv); else return -1;
            ++argv;
            if (*argv) bits_sample = atoi(*argv); else return -1;
            channels = 2;
            if (sample_rate < 1 || (bits_sample != 8 && bits_sample != 16 && bits_sample != 32)) {
                fprintf(stderr, "- <sr> <bs>\n");
                return -1;
            }
            pcm.sr  = sample_rate;
            pcm.bps = bits_sample;
            pcm.nch = channels;
            option_pcmraw = 1;
        }
        else {
            fp = fopen(*argv, "rb");
            if (fp == NULL) {
                fprintf(stderr, "error: open %s\n", *argv);
                return -1;
            }
            wavloaded = 1;
        }
        ++argv;
    }
    if (!wavloaded) fp = stdin;

    if (option_iq == 5 && option_dc) option_lp |= LP_FM;

    if (option_noLUT && option_iq == 5) dsp.opt_nolut = 1; else dsp.opt_nolut = 0;


    if (gpx->option.raw == 4) gpx->option.ecc = 1;

    memcpy(gpx->frame, frm_sync6, sizeof(frm_sync6));
    gpx->frm_pos = 0;
    gpx->sf6 = 0;
    gpx->sfX = 0;
    for (k = 0; k < strlen(blk_syncbits); k++) {
        int hbit = blk_syncbits[k] & 1;
        gpx->blk_rawbits[k].hb = hbit + 0x30;
        gpx->blk_rawbits[k].sb = 2*hbit-1;
    }


    gpx->option.inv = option_inv;

    gpx->week = gpsweek;

    if (cfreq > 0) gpx->jsn_freq = (cfreq+500)/1000;


    #ifdef EXT_FSK
    if (!option_softin) {
        option_softin = 1;
        fprintf(stderr, "reading float32 soft symbols\n");
    }
    #endif

    if (!option_softin) {

        if (option_iq == 0 && option_pcmraw) {
            fclose(fp);
            fprintf(stderr, "error: raw data not IQ\n");
            return -1;
        }
        if (option_iq == 0 && gpx->option.vit == 2) {
            gpx->option.vit = 1;
            fprintf(stderr, "info: soft decoding only for IQ\n");
        }
        if (option_iq) sel_wavch = 0;

        pcm.sel_ch = sel_wavch;
        if (option_pcmraw == 0) {
            k = read_wav_header(&pcm, fp);
            if ( k < 0 ) {
                fclose(fp);
                fprintf(stderr, "error: wav header\n");
                return -1;
            }
        }

        if (cfreq > 0) {
            int fq_kHz = (cfreq - dsp.xlt_fq*pcm.sr + 500)/1e3;
            gpx->jsn_freq = fq_kHz;
        }

        symlen = 1;

        dsp.fp = fp;
        dsp.sr = pcm.sr;
        dsp.bps = pcm.bps;
        dsp.nch = pcm.nch;
        dsp.ch = pcm.sel_ch;
        dsp.br = (float)BAUD_RATE6;
        dsp.sps = (float)dsp.sr/dsp.br;
        dsp.symlen = symlen;
        dsp.symhd = 1;
        dsp._spb = dsp.sps*symlen;
        dsp.hdr = rawheader;
        dsp.hdrlen = strlen(rawheader);
        dsp.BT = 1.2;
        dsp.h = 0.9;
        dsp.opt_iq = option_iq;
        dsp.opt_iqdc = option_iqdc;
        dsp.opt_lp = option_lp;
        dsp.lpIQ_bw = lpIQ_bw;
        dsp.lpFM_bw = 6e3;
        dsp.opt_dc = option_dc;
        dsp.opt_IFmin = option_min;

        if ( dsp.sps < 8 ) {
            fprintf(stderr, "note: sample rate low (%.1f sps)\n", dsp.sps);
        }



        k = init_buffers(&dsp);
        if ( k < 0 ) {
            fprintf(stderr, "error: init buffers\n");
            return -1;
        }
    }
    else {
        hdb.hdr = rawheader;
        hdb.len = strlen(rawheader);
        hdb.bufpos = -1;
        hdb.buf = NULL;
        hdb.ths = 0.7;
        hdb.sbuf = calloc(hdb.len, sizeof(float));
        if (hdb.sbuf == NULL) {
            fprintf(stderr, "error: malloc\n");
            return -1;
        }
    }


    if (gpx->option.vit) {
        k = vit_initCodes(gpx);
        if (k < 0) return -1;
    }
    if (gpx->option.ecc) {
        rs_init_RS255ccsds(&gpx->RS);
    }


    bitofs = bitofs6 + shift;
    rawbitblock_len = RAWBITBLOCK_LEN_6;
    if (gpx->auto_detect) gpx->typ = 6;

    if (gpx->auto_detect == 0) {
        if (gpx->typ == 6) {
            rawbitblock_len = RAWBITBLOCK_LEN_6;
            dsp.br = (float)BAUD_RATE6;
            dsp.sps = (float)dsp.sr/dsp.br;
            bitofs = bitofs6 + shift;
        }
        if (gpx->typ == 10) {
            rawbitblock_len = RAWBITBLOCK_LEN;
            dsp.br = (float)BAUD_RATEX;
            dsp.sps = (float)dsp.sr/dsp.br;
            bitofs = bitofsX + shift;
        }
    }


    while ( 1 )
    {
        if (option_softin) {
            header_found = find_softbinhead(fp, &hdb, &_mv, option_softin == 2);
        }
        else {
            header_found = find_header(&dsp, thres, 10, bitofs, dsp.opt_dc);
            _mv = dsp.mv;
        }

        if (header_found == EOF) break;

        if (_mv*(0.5-gpx->option.inv) < 0) {
            gpx->option.inv ^= 0x1;
        }

        if (header_found) {

            gpx->frm_rate = 4800.0 * dsp.sr/(double)(dsp.mv_pos - mpos0);
            mpos0 = dsp.mv_pos;


            bitpos = 0;
            pos = BLOCKSTART;

            if (_mv > 0) bc = 0; else bc = 1;

            while ( pos < rawbitblock_len ) {

               if (option_softin) {
                    float s = 0.0;
                    bitQ = f32soft_read(fp, &s, option_softin == 2);
                    if (bitQ != EOF) {
                        rhsbit.sb = s;
                        rhsbit.hb = (s>=0.0);
                    }
                }
                else {
                    bitQ = read_softbit2p(&dsp, &rhsbit, 0, bitofs, bitpos, -1, 0, &rhsbit1);
                    if (gpx->option.ecc == 3) {
                    if (rhsbit.sb*rhsbit1.sb < 0) {
                        rhsbit.sb += rhsbit1.sb;
                        rhsbit.hb = (rhsbit.sb>=0.0);
                    }
                    }
                }
                if (bitQ == EOF) { break; }


                hsbit.hb = rhsbit.hb ^ (bc%2);
                int sgn = -2*(((unsigned int)bc)%2)+1;
                hsbit.sb = sgn * rhsbit.sb;

                if (gpx->option.vit == 1) {
                    hsbit.sb = 2*hsbit.hb -1;
                }

                gpx->blk_rawbits[pos] = hsbit;
                gpx->blk_rawbits[pos].hb += 0x30;

                bc++;
                pos++;
                bitpos += 1;
            }

            gpx->blk_rawbits[pos].hb = '\0';

            time_elapsed_sec = dsp.sample_in / (double)dsp.sr;
            proc_frame(gpx, pos);

            if (pos < rawbitblock_len) break;

            pos = BLOCKSTART;
            header_found = 0;

            if ( gpx->auto_detect && gpx->reset_dsp ) {
                if (gpx->typ == 10) {
                    rawbitblock_len = RAWBITBLOCK_LEN;
                    dsp.br = (float)BAUD_RATEX;
                    dsp.sps = (float)dsp.sr/dsp.br;

                    for (k = 0; k < dsp.N_IQBUF; k++) dsp.rot_iqbuf[k] = 0;
                    dsp.F1sum = 0;
                    dsp.F2sum = 0;

                    bitofs = bitofsX + shift;
                }
                if ((gpx->typ & 0xFF) == 6) {
                    rawbitblock_len = RAWBITBLOCK_LEN_6;
                    dsp.br = (float)BAUD_RATE6;
                    dsp.sps = (float)dsp.sr/dsp.br;

                    for (k = 0; k < dsp.N_IQBUF; k++) dsp.rot_iqbuf[k] = 0;
                    dsp.F1sum = 0;
                    dsp.F2sum = 0;

                    bitofs = bitofs6 + shift;
                }
                gpx->reset_dsp = 0;
            }

        }
    }


    if (!option_softin) free_buffers(&dsp);
    else {
        if (hdb.buf) { free(hdb.buf); hdb.buf = NULL; }
    }

    if (gpx->vit) { free(gpx->vit); gpx->vit = NULL; }

    fclose(fp);

    return 0;
}

