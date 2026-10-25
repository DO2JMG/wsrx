

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
    i8_t ecc;
    i8_t ptu;
    i8_t inv;
    i8_t aut;
    i8_t jsn;
    i8_t slt;
} option_t;


#define BAUD_RATE   2400

#define OFS 5

#define OFS_CF06MSG1 (OFS+7)
#define LEN_CF06MSG1 (42)
#define OFS_CF06MSG2 (OFS+55)
#define LEN_CF06MSG2 (41)

#define FRAME_LEN    109
#define BITFRAME_LEN (8*FRAME_LEN)

#define HEADLEN     64
#define HEADOFS     0
#define FRAMESTART (HEADLEN)

static char cf06ht03_header[] = "01010101""01010101""01010101""01010101""01010101"
                                "10110100""00101011""11000110";


typedef struct {
    int frnr;
    ui32_t sn;
    char id[8+4];
    int gpstow;
    int wday;
    int std; int min; float sek;
    float lat; float lon; float alt;
    float vH; float vD; float vV;
    float T; float RH; float P;
    char  frame_bits[BITFRAME_LEN+1];
    ui8_t frame_bytes[FRAME_LEN+1];
    int jsn_freq;
    option_t option;
    RS_t RS;
} gpx_t;

static RS_t RS_CF06 = { .N=255, .t=3, .R=6, .K=249, .b=1, .p=1, 1, {0}, {0} };


static ui32_t crc16(ui8_t bytes[], int len) {
    ui32_t crc16poly = 0x1021;
    ui32_t rem = 0;
    int i, j;
    for (i = 0; i < len; i++) {
        rem = rem ^ (bytes[i] << 8);
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

static int cf06_ecc(gpx_t *gpx, int *errs1, int *errs2) {
    ui8_t cw[255];
    ui8_t err_pos[255], err_val[255];
    int errors1, errors2;
    int parlen = RS_CF06.R;
    int ret = 0;
    int j;

    memset(cw, 0, 255);
    for (j = 0; j < LEN_CF06MSG1; j++) cw[255-1-j] = gpx->frame_bytes[OFS_CF06MSG1+j];
    for (j = 0; j < parlen;  j++) cw[255-1-LEN_CF06MSG1-j] = gpx->frame_bytes[OFS_CF06MSG1+LEN_CF06MSG1+j];
    errors1 = rs_decode(&gpx->RS, cw, err_pos, err_val);
    for (j = 0; j < errors1; j++) {
        if (err_pos[j] < 255-LEN_CF06MSG1-parlen) errors1 = -5;
    }
    if (errors1 > 0) {
        for (j = 0; j < LEN_CF06MSG1; j++) gpx->frame_bytes[OFS_CF06MSG1+j] = cw[255-1-j];
        for (j = 0; j < parlen;  j++) gpx->frame_bytes[OFS_CF06MSG1+LEN_CF06MSG1+j] = cw[255-1-LEN_CF06MSG1-j];
    }

    memset(cw, 0, 255);
    for (j = 0; j < LEN_CF06MSG2; j++) cw[255-1-j] = gpx->frame_bytes[OFS_CF06MSG2+j];
    for (j = 0; j < parlen;  j++) cw[255-1-LEN_CF06MSG2-j] = gpx->frame_bytes[OFS_CF06MSG2+LEN_CF06MSG2+j];
    errors2 = rs_decode(&gpx->RS, cw, err_pos, err_val);
    for (j = 0; j < errors2; j++) {
        if (err_pos[j] < 255-LEN_CF06MSG2-parlen) errors2 = -5;
    }
    if (errors2 > 0) {
        for (j = 0; j < LEN_CF06MSG2; j++) gpx->frame_bytes[OFS_CF06MSG2+j] = cw[255-1-j];
        for (j = 0; j < parlen;  j++) gpx->frame_bytes[OFS_CF06MSG2+LEN_CF06MSG2+j] = cw[255-1-LEN_CF06MSG2-j];
    }

    *errs1 = errors1;
    *errs2 = errors2;

    return ret;
}

static char weekday[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static int print_cf06(gpx_t *gpx) {
    int j;
    int crcdat1, crcval1, crc_ok1,
        crcdat2, crcval2, crc_ok2;
    int errs1=0, errs2=0;

    if (gpx->option.ecc == 2) {
        for (j = 0; j < 10; j++) gpx->frame_bytes[OFS_CF06MSG2+j] = 0xAA;
    }

    if (gpx->option.ecc) {
        cf06_ecc(gpx, &errs1, &errs2);
    }

    crcdat1 = (gpx->frame_bytes[OFS+47]<<8) | gpx->frame_bytes[OFS+47+1];
    crcval1 = crc16(gpx->frame_bytes+OFS_CF06MSG1, LEN_CF06MSG1-2);
    crc_ok1 = (crcdat1 == crcval1);
    crcdat2 = (gpx->frame_bytes[OFS+94]<<8) | gpx->frame_bytes[OFS+94+1];
    crcval2 = crc16(gpx->frame_bytes+OFS_CF06MSG2, LEN_CF06MSG2-2);
    crc_ok2 = (crcdat2 == crcval2);

    if (gpx->option.raw) {
        if (gpx->option.raw == 1) {
            for (j = 0; j < FRAME_LEN; j++) {
                printf("%02X ", gpx->frame_bytes[j]);
            }
            printf(" #  [%s,%s]", crc_ok1 ? "OK1" : "NO1", crc_ok2 ? "OK2" : "NO2");
            if (gpx->option.ecc && (errs1 || errs2)) printf("  (%d,%d)", errs1, errs2);
        }
        else {
            for (j = 0; j < BITFRAME_LEN; j++) {
                printf("%c", gpx->frame_bits[j]);
                if (j % 8 == 7) printf(" ");
            }
        }
        printf("\n");
    }
    if (gpx->option.jsn || !gpx->option.raw)
    {
        ui32_t sn;
        ui32_t val;
        int ival;
        ui32_t gpstime;
        ui32_t ms;
        int wday;
        i16_t val16;
        float vN, vE, vU, vH, vD;

        sn = 0;
        for (j = 0; j < 4; j++) {
            sn *= 100;
            sn += gpx->frame_bytes[OFS+7+j] % 100;
        }
        gpx->sn = sn;
        sprintf(gpx->id, "%08d", gpx->sn);

        gpstime = 0;
        for (j = 0; j < 4; j++) gpstime |= gpx->frame_bytes[OFS+11+j] << (8*j);

        gpx->gpstow = gpstime;
        gpx->frnr = (gpx->gpstow + 500)/1000;

        ms = gpstime % 1000;
        gpstime /= 1000;

        wday = (gpstime / (24 * 3600)) % 7;
        if ((wday < 0) || (wday > 6)) wday = 0;
        gpx->wday = wday;

        gpstime %= (24*3600);

        gpx->std =  gpstime / 3600;
        gpx->min = (gpstime % 3600) / 60;
        gpx->sek =  gpstime % 60 + ms/1000.0;

        val = 0;
        for (j = 0; j < 4; j++) val |= gpx->frame_bytes[OFS+15+j] << (8*j);
        ival = (int)val;
        gpx->lon = ival / 1e7;

        val = 0;
        for (j = 0; j < 4; j++) val |= gpx->frame_bytes[OFS+19+j] << (8*j);
        ival = (int)val;
        gpx->lat = ival / 1e7;

        val = 0;
        for (j = 0; j < 4; j++) val |= gpx->frame_bytes[OFS+23+j] << (8*j);
        ival = (int)val;
        gpx->alt = ival / 1e3;


        val16 = 0;
        for (j = 0; j < 2; j++) val16 |= gpx->frame_bytes[OFS+27+j] << (8*j);
        vE = val16/100.0f;
        val16 = 0;
        for (j = 0; j < 2; j++) val16 |= gpx->frame_bytes[OFS+29+j] << (8*j);
        vN = val16/100.0f;
        val16 = 0;
        for (j = 0; j < 2; j++) val16 |= gpx->frame_bytes[OFS+31+j] << (8*j);
        vU = -val16/100.0f;
        vH = sqrt(vN*vN+vE*vE);
        vD = atan2(vE, vN) * 180.0 / M_PI;
        if (vD < 0) vD += 360;
        gpx->vH = vH;
        gpx->vD = vD;
        gpx->vV = vU;


        gpx->T = -273.15f;
        gpx->RH = -1.0f;
        gpx->P = -1.0f;


        if (!gpx->option.raw)
        {
            printf(" (%08d)  ", gpx->sn);
            printf("%s ", weekday[gpx->wday]);
            printf("%02d:%02d:%06.3f ", gpx->std, gpx->min, gpx->sek);
            printf(" ");
            printf(" lon: %.4f ", gpx->lon);
            printf(" lat: %.4f ", gpx->lat);
            printf(" alt: %.1f ", gpx->alt);
            printf("  vH: %4.1f  D: %5.1f  vV: %3.1f ", gpx->vH, gpx->vD, gpx->vV);
            printf(" ");

            val = gpx->frame_bytes[OFS+45];
            printf("[%3d] ", val);

            printf(" ");

            printf(" %s", crc_ok1 ? "[OK1]" : "[NO1]");
            if (gpx->option.vbs) printf(" # [%04X:%04X]", crcdat1, crcval1);
            if (gpx->option.vbs) {
                printf(" ");
                printf(" %s", crc_ok2 ? "[OK2]" : "[NO2]");
                if (gpx->option.vbs) printf(" # [%04X:%04X]", crcdat2, crcval2);

                if (gpx->option.ecc && (errs1 || errs2)) printf("  (%d,%d)", errs1, errs2);
            }

            printf("\n");
        }
    }

    return crc_ok1;
}

static ui32_t crc16rev(ui8_t bytes[], int len) {
    ui32_t crc16poly = 0x8408;
    ui32_t rem = 0;
    int i, j;
    for (i = 0; i < len; i++) {
        rem = rem ^ bytes[i];
        for (j = 0; j < 8; j++) {
            if (rem & 0x0001) {
                rem = (rem >> 1) ^ crc16poly;
            }
            else {
                rem = (rem >> 1);
            }
            rem &= 0xFFFF;
        }
    }
    return rem;
}

static int print_ht03(gpx_t *gpx) {
    int j;
    int crcdat, crcval, crc_ok;

    crcdat = gpx->frame_bytes[OFS+100] | (gpx->frame_bytes[OFS+100+1]<<8);
    crcval = crc16rev(gpx->frame_bytes+OFS+3, 97);
    crc_ok = (crcdat == crcval);

    if (gpx->option.raw) {
        if (gpx->option.raw == 1) {
            for (j = 0; j < FRAME_LEN; j++) {
                printf("%02X ", gpx->frame_bytes[j]);
            }
            printf(" #  %s", crc_ok ? "[OK]" : "[NO]");
        }
        else {
            for (j = 0; j < BITFRAME_LEN; j++) {
                printf("%c", gpx->frame_bits[j]);
                if (j % 8 == 7) printf(" ");
            }
        }
        printf("\n");
    }
    if (gpx->option.jsn || !gpx->option.raw)
    {
        ui32_t sn;
        int val;
        i16_t val16;
        float *fval;
        float vN, vE, vU, vH, vD;

        sn = 0;
        for (j = 0; j < 4; j++) sn |= gpx->frame_bytes[OFS+7+j] << (8*(3-j));
        gpx->sn = sn;
        sprintf(gpx->id, "%08X", gpx->sn);

        gpx->std = gpx->frame_bytes[OFS+12] & 0x1F;
        gpx->min = gpx->frame_bytes[OFS+13] & 0x3F;
        gpx->sek = (gpx->frame_bytes[OFS+14] | (gpx->frame_bytes[OFS+13] & 0xC0) << 2) / 10.0f;


        fval = (float*)(gpx->frame_bytes+OFS+16);
        gpx->lon = *fval * 180.0 / M_PI;

        fval = (float*)(gpx->frame_bytes+OFS+20);
        gpx->lat = *fval * 180.0 / M_PI;

        val = 0;
        for (j = 0; j < 4; j++) val |= gpx->frame_bytes[OFS+24+j] << (8*j);
        gpx->alt = val/10.0f;

        val16 = 0;
        for (j = 0; j < 2; j++) val16 |= gpx->frame_bytes[OFS+28+j] << (8*j);
        vN = val16/100.0f;
        val16 = 0;
        for (j = 0; j < 2; j++) val16 |= gpx->frame_bytes[OFS+30+j] << (8*j);
        vE = val16/100.0f;
        val16 = 0;
        for (j = 0; j < 2; j++) val16 |= gpx->frame_bytes[OFS+32+j] << (8*j);
        vU = val16/100.0f;
        vH = sqrt(vN*vN+vE*vE);
        vD = atan2(vE, vN) * 180.0 / M_PI;
        if (vD < 0) vD += 360;
        gpx->vH = vH;
        gpx->vD = vD;
        gpx->vV = vU;

        val16 = 0;
        for (j = 0; j < 2; j++) val16 |= gpx->frame_bytes[OFS+84+j] << (8*j);
        gpx->T = val16/100.0f;

        val16 = 0;
        for (j = 0; j < 2; j++) val16 |= gpx->frame_bytes[OFS+88+j] << (8*j);
        gpx->RH = val16/100.0f;

        val = 0;
        for (j = 0; j < 3; j++) val |= gpx->frame_bytes[OFS+90+j] << (8*j);
        gpx->P = val/100.0f;


        val = 0;
        for (j = 0; j < 2; j++) val |= gpx->frame_bytes[OFS+97+j] << (8*(1-j));
        gpx->frnr = val;


        if (!gpx->option.raw)
        {
            printf(" (%08X) ", gpx->sn);
            printf(" ");
            printf("%02d:%02d:%04.1f ", gpx->std, gpx->min, gpx->sek);
            printf(" ");
            printf(" lon: %.4f ", gpx->lon);
            printf(" lat: %.4f ", gpx->lat);
            printf(" alt: %.1f ", gpx->alt);
            printf("  vH: %4.1f  D: %5.1f  vV: %3.1f ", gpx->vH, gpx->vD, gpx->vV);
            printf(" ");
            if (crc_ok) {
                if (gpx->T > -273.0) printf(" T=%.1fC ", gpx->T);
                if (gpx->RH > -0.5) printf(" RH=%.0f%% ", gpx->RH);
                if (gpx->P > 0.0 && gpx->P < 2e3) {
                    if (gpx->P < 100.0) printf(" P=%.2fhPa ", gpx->P);
                    else                printf(" P=%.1fhPa ", gpx->P);
                }
                printf(" ");
            }
            printf(" [%5d] ", gpx->frnr);
            printf(" ");

            printf(" %s", crc_ok ? "[OK]" : "[NO]");
            if (gpx->option.vbs) printf(" # [%04X:%04X]", crcdat, crcval);

            printf("\n");
        }
    }

    return crc_ok;
}

#define CF06 6
#define HT03 3

static int print_frame(gpx_t *gpx, int len_bytes, int b2B) {
    int frm_ok = 0;
    int rs_typ = 0;
    int i, j;
    char *rs_str = "";

    if (len_bytes > FRAME_LEN) len_bytes = FRAME_LEN;

    if (b2B)
    {
        for (j = 0; j < FRAME_LEN; j++) {
            ui8_t byteval = 0;
            ui8_t d = 1;
            if (j < len_bytes) {
                for (i = 0; i < 8; i++) {
                    if (gpx->frame_bits[8*j+i] & 1) byteval += d;
                    d <<= 1;
                }
            }
            gpx->frame_bytes[j] = byteval;
        }
    }

    if  (gpx->frame_bytes[OFS+5] == 0xAA && gpx->frame_bytes[OFS+6] == 0xAA)  rs_typ = CF06;
    else
    if  (gpx->frame_bytes[OFS+5] == 0xFF && gpx->frame_bytes[OFS+6] == 0xFF)  rs_typ = HT03;
    else rs_typ = HT03;

    switch (rs_typ)
    {
        case CF06:  frm_ok = print_cf06(gpx);
                    rs_str = "CF6";
                    break;
        case HT03:  frm_ok = print_ht03(gpx);
                    rs_str = "GTH";
                    break;
        default:
                    break;
    }

    if (frm_ok && gpx->frnr == 0 && gpx->sn == 0) frm_ok = 0;
    if (gpx->option.jsn && frm_ok) {
        char *ver_jsn = NULL;
        printf("{ \"type\": \"%s\"", rs_str);
        printf(", \"frame\": %d, \"id\": \"%.4s-%.8s\", \"datetime\": \"%02d:%02d:%06.3fZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f, \"vel_h\": %.5f, \"heading\": %.5f, \"vel_v\": %.5f",
               gpx->frnr, rs_str, gpx->id, gpx->std, gpx->min, gpx->sek, gpx->lat, gpx->lon, gpx->alt, gpx->vH, gpx->vD, gpx->vV );
        if (gpx->T > -273.0) {
            printf(", \"temp\": %.2f",  gpx->T );
        }
        if (gpx->RH > -0.5) {
            printf(", \"humidity\": %.2f", gpx->RH );
        }
        if (gpx->P > 0.0) {
            printf(", \"pressure\": %.2f",  gpx->P );
        }

        if (gpx->jsn_freq > 0) {
            printf(", \"freq\": %d", gpx->jsn_freq);
        }

        printf(", \"ref_datetime\": \"%s\"", (rs_typ == CF06) ? "GPS" : "UTC" );
        printf(", \"ref_position\": \"%s\"", "MSL" );

        #ifdef VER_JSN_STR
            ver_jsn = VER_JSN_STR;
        #endif
        if (ver_jsn && *ver_jsn != '\0') printf(", \"version\": \"%s\"", ver_jsn);
        printf(" }\n");
        printf("\n");
    }

    return 0;
}

int main(int argc, char **argv) {

    FILE *fp;
    char *fpname;

    int option_timestamp = 0;

    int k;
    int bitpos;
    int bit;
    int bitQ;

    int cfreq = -1;
    int option_bin = 0;
    int option_softin = 0;
    int option_pcmraw = 0;
    int wavloaded = 0;
    int sel_wavch = 0;
    int rawhex = 0;
    int header_found = 0;

    float thres = 0.7;
    float _mv = 0.0;

    float lpIQ_bw = 7.4e3;

    int symlen = 1;
    int bitofs = 1;
    int shift = 0;

    pcm_t pcm = {0};
    dsp_t dsp = {0};
    gpx_t gpx = {0};
    hdb_t hdb = {0};
    hsbit_t hsbit, hsbit1;


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
            fprintf(stderr, "       -v     (verbose)\n");
            fprintf(stderr, "       -r     (raw)\n");
            fprintf(stderr, "       --ecc  (CF-06: ECC)\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            gpx.option.inv = 1;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            gpx.option.vbs = 1;
        }
        else if   (strcmp(*argv, "-t" ) == 0) { option_timestamp = 1; }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            gpx.option.raw = 1;
        }
        else if ( (strcmp(*argv, "-R") == 0) || (strcmp(*argv, "--RAW") == 0) ) {
            gpx.option.raw = 2;
        }
        else if   (strcmp(*argv, "--ecc" ) == 0) { gpx.option.ecc = 1; }
        else if   (strcmp(*argv, "--ecc2") == 0) { gpx.option.ecc = 2; }
        else if   (strcmp(*argv, "--auto") == 0) { gpx.option.aut = 1; }
        else if   (strcmp(*argv, "--bin") == 0) { option_bin = 1; }
        else if   (strcmp(*argv, "--softin") == 0)  { option_softin = 1; }
        else if   (strcmp(*argv, "--softinv") == 0) { option_softin = 2; }
        else if   (strcmp(*argv, "--iq0") == 0) { dsp.opt_iq = 1; }
        else if   (strcmp(*argv, "--iq2") == 0) { dsp.opt_iq = 2; }
        else if   (strcmp(*argv, "--iq3") == 0) { dsp.opt_iq = 3; }
        else if   (strcmp(*argv, "--iqdc") == 0) { dsp.opt_iqdc = 1; }
        else if   (strcmp(*argv, "--IQ") == 0) {
            double fq = 0.0;
            ++argv;
            if (*argv) fq = atof(*argv);
            else return -1;
            if (fq < -0.5) fq = -0.5;
            if (fq >  0.5) fq =  0.5;
            dsp.xlt_fq = -fq;
            dsp.opt_iq = 5;
        }
        else if   (strcmp(*argv, "--lpIQ") == 0) { dsp.opt_lp |= LP_IQ; }
        else if   (strcmp(*argv, "--lpbw") == 0) {
            double bw = 0.0;
            ++argv;
            if (*argv) bw = atof(*argv);
            else return -1;
            if (bw > 4.6 && bw < 24.0) lpIQ_bw = bw*1e3;
            dsp.opt_lp |= LP_IQ;
        }
        else if   (strcmp(*argv, "--lpFM") == 0) { dsp.opt_lp |= LP_FM; }
        else if   (strcmp(*argv, "--dc") == 0) { dsp.opt_dc = 1; }
        else if   (strcmp(*argv, "--noLUT") == 0) { dsp.opt_nolut = 1; }
        else if   (strcmp(*argv, "--min") == 0) {
            dsp.opt_IFmin = 1;
        }
        else if   (strcmp(*argv, "--json") == 0) {
            gpx.option.jsn = 1;
        }
        else if   (strcmp(*argv, "--jsn_cfq") == 0) {
            int frq = -1;
            ++argv;
            if (*argv) frq = atoi(*argv); else return -1;
            if (frq < 300000000) frq = -1;
            cfreq = frq;
        }
        else if (strcmp(*argv, "--rawhex") == 0) { rawhex = 3; }
        else if   (strcmp(*argv, "--ch2") == 0) { sel_wavch = 1; }
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

    if (dsp.opt_iq == 5 && dsp.opt_dc) dsp.opt_lp |= LP_FM;

    if (dsp.opt_iq != 5) dsp.opt_nolut = 0;

    gpx.RS = RS_CF06;
    gpx.RS.GF = GF256RS;
    rs_init_RS(&gpx.RS);

    memcpy(gpx.frame_bits, cf06ht03_header, sizeof(cf06ht03_header));

    if (cfreq > 0) gpx.jsn_freq = (cfreq+500)/1000;


    #ifdef EXT_FSK
    if (!option_bin && !option_softin) {
        option_softin = 1;
        fprintf(stderr, "reading float32 soft symbols\n");
    }
    #endif

    if (!rawhex) {

        if (!option_bin && !option_softin) {

            if (dsp.opt_iq == 0 && option_pcmraw) {
                fclose(fp);
                fprintf(stderr, "error: raw data not IQ\n");
                return -1;
            }
            if (dsp.opt_iq) sel_wavch = 0;

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
                gpx.jsn_freq = fq_kHz;
            }

            symlen = 1;

            dsp.fp = fp;
            dsp.sr = pcm.sr;
            dsp.bps = pcm.bps;
            dsp.nch = pcm.nch;
            dsp.ch = pcm.sel_ch;
            dsp.br = (float)BAUD_RATE;
            dsp.sps = (float)dsp.sr/dsp.br;
            dsp.symlen = symlen;
            dsp.symhd  = symlen;
            dsp._spb = dsp.sps*symlen;
            dsp.hdr = cf06ht03_header;
            dsp.hdrlen = strlen(cf06ht03_header);
            dsp.BT = 1.0;
            dsp.h = 1.8;
            dsp.lpIQ_bw = lpIQ_bw;
            dsp.lpFM_bw = 6e3;

            if ( dsp.sps < 8 ) {
                fprintf(stderr, "note: sample rate low (%.1f sps)\n", dsp.sps);
            }


            k = init_buffers(&dsp);
            if ( k < 0 ) {
                fprintf(stderr, "error: init buffers\n");
                return -1;
            }

            bitofs += shift;
        }
        else {
            if (option_bin && option_softin) option_bin = 0;
            hdb.hdr = cf06ht03_header;
            hdb.len = strlen(cf06ht03_header);
            hdb.thb = 1.0 - 3.1/(float)hdb.len;
            hdb.bufpos = -1;
            hdb.buf = calloc(hdb.len, sizeof(char));
            if (hdb.buf == NULL) {
                fprintf(stderr, "error: malloc\n");
                return -1;
            }
            hdb.ths = 0.8;
            hdb.sbuf = calloc(hdb.len, sizeof(float));
            if (hdb.sbuf == NULL) {
                fprintf(stderr, "error: malloc\n");
                return -1;
            }
        }

        while ( 1 )
        {
            if (option_bin) {
                header_found = find_binhead(fp, &hdb, &_mv);
            }
            else if (option_softin) {
                header_found = find_softbinhead(fp, &hdb, &_mv, option_softin == 2);
            }
            else {
                header_found = find_header(&dsp, thres, 4, bitofs, dsp.opt_dc);
                _mv = dsp.mv;
            }
            if (header_found == EOF) break;

            if (_mv *(0.5-gpx.option.inv) < 0) {
                if (gpx.option.aut == 0) header_found = 0;
                else gpx.option.inv ^= 0x1;
            }

            if (header_found)
            {
                bitpos = FRAMESTART;

                while ( bitpos < BITFRAME_LEN )
                {
                    if (option_bin) {
                        bitQ = fgetc(fp);
                        if (bitQ != EOF) {
                            bit = bitQ & 0x1;
                            hsbit.hb = bit;
                            hsbit.sb = 2*bit-1;
                        }
                    }
                    else if (option_softin) {
                        float s = 0.0;
                        bitQ = f32soft_read(fp, &s, option_softin == 2);
                        if (bitQ != EOF) {
                            bit = (s>=0.0);
                            hsbit.hb = bit;
                            hsbit.sb = s;
                        }
                    }
                    else {
                        float bl = -1;
                        if (dsp.opt_iq > 2) bl = 2.0;
                        bitQ = read_softbit2p(&dsp, &hsbit, 0, bitofs, bitpos-FRAMESTART, bl, 0, &hsbit1);
                        bit = hsbit.hb;
                        if (gpx.option.ecc >= 2) bit = (hsbit.sb+hsbit1.sb)>=0;
                    }
                    if ( bitQ == EOF ) break;

                    if (gpx.option.inv) {
                        bit ^= 1;
                        hsbit.hb ^= 1;
                        hsbit.sb = -hsbit.sb;
                    }

                    gpx.frame_bits[bitpos] = 0x30 + bit;
                    bitpos += 1;
                }

                header_found = 0;

                print_frame(&gpx, bitpos/8, 1);
            }
        }

        if (!option_bin && !option_softin) free_buffers(&dsp);
        else {
            if (hdb.buf) { free(hdb.buf); hdb.buf = NULL; }
        }
    }
    else
    {
        char buffer_rawhex[3*FRAME_LEN+12];
        char *pbuf = NULL, *buf_sp = NULL;
        ui8_t frmbyte;
        int frameofs = 0, len, i;

        while (1 > 0) {

            pbuf = fgets(buffer_rawhex, 3*FRAME_LEN+12, fp);
            if (pbuf == NULL) break;
            buffer_rawhex[3*FRAME_LEN] = '\0';
            buf_sp = strchr(buffer_rawhex, '[');
            if (buf_sp != NULL && buf_sp-buffer_rawhex < 3*FRAME_LEN) {
                buffer_rawhex[buf_sp-buffer_rawhex] = '\0';
            }
            len = strlen(buffer_rawhex) / 3;
            if (len > 20) {
                for (i = 0; i < len; i++) {
                    sscanf(buffer_rawhex+3*i, "%2hhx", &frmbyte);
                    gpx.frame_bytes[frameofs+i] = frmbyte;
                }
                print_frame(&gpx, len, 0);
            }
        }
    }

    fclose(fp);

    return 0;
}

