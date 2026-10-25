
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
    i8_t dwp;
    i8_t aux;
    i8_t inv;
    i8_t aut;
    i8_t jsn;
    i8_t slt;
    i8_t cal;
} option_t;

typedef struct {
    int typ;
    int msglen;
    int msgpos;
    int parpos;
    int hdrlen;
    int frmlen;
} rscfg_t;

static rscfg_t cfg_rs41 = { 41, (320-56)/2, 56, 8, 8, 320};


#define NDATA_LEN 320
#define XDATA_LEN 198
#define FRAME_LEN (NDATA_LEN+XDATA_LEN)

typedef struct {
    float frm_bytescore[FRAME_LEN+8];
    float ts;
    float last_frnb_ts;
    float last_calfrm_ts;
    ui16_t last_frnb;
    ui8_t  last_calfrm;
    int sort_idx1[FRAME_LEN];
    int sort_idx2[FRAME_LEN];
} ecdat_t;

typedef struct {
    ui8_t id168;
    ui8_t status;
} gnss_t;

typedef struct {
    int out;
    int frnr;
    char id[9];
    ui8_t numSV;
    ui8_t gnss_numSVb168;
    ui8_t gnss_nSVstatus;
    gnss_t gnss_sv[32];
    ui8_t isUTC;
    int week; int tow_ms; int gpssec;
    int jahr; int monat; int tag;
    int wday;
    int std; int min; float sek;
    double lat; double lon; double alt;
    double vH; double vD; double vV;
    float T; float RH; float TH;
    float P; float RH2;
    ui32_t crc;
    ui8_t frame[FRAME_LEN];
    ui8_t dfrm_bitscore[FRAME_LEN];
    ui8_t calibytes[51*16];
    ui8_t calfrchk[51];
    ui8_t calconf_complete;
    ui8_t calconf_sent;
    ui8_t *calconf_subfrm;
    float ptu_Rf1;
    float ptu_Rf2;
    float ptu_co1[3];
    float ptu_calT1[3];
    float ptu_co2[3];
    float ptu_calT2[3];
    float ptu_calH[2];
    float ptu_mtxH[42];
    float ptu_corHp[3];
    float ptu_corHt[12];
    float ptu_Cf1;
    float ptu_Cf2;
    float ptu_calP[25];
    ui32_t freq;
    int jsn_freq;
    float batt;
    int txpower;
    ui8_t txpower_valid;
    ui16_t conf_fw;
    ui16_t conf_kt;
    ui16_t conf_bt;
    ui16_t conf_cd;
    ui8_t  conf_bk;
    char rstyp[10];
    char rstmp[10];
    char rsm[10];
    int  aux;
    char xdata[XDATA_LEN+16];
    option_t option;
    RS_t RS;
    ecdat_t ecdat;
} gpx_t;


#define BITS    8
#define HEADLEN 64
#define FRAMESTART ((HEADLEN)/BITS)

static char rs41_header[] = "0000100001101101010100111000100001000100011010010100100000011111";

static ui8_t rs41_header_bytes[8] = { 0x86, 0x35, 0xf4, 0x40, 0x93, 0xdf, 0x1a, 0x60};

#define MASK_LEN 64
static ui8_t mask[MASK_LEN] = { 0x96, 0x83, 0x3E, 0x51, 0xB1, 0x49, 0x08, 0x98,
                                0x32, 0x05, 0x59, 0x0E, 0xF9, 0x44, 0xC6, 0x26,
                                0x21, 0x60, 0xC2, 0xEA, 0x79, 0x5D, 0x6D, 0xA1,
                                0x54, 0x69, 0x47, 0x0C, 0xDC, 0xE8, 0x5C, 0xF1,
                                0xF7, 0x76, 0x82, 0x7F, 0x07, 0x99, 0xA2, 0x2C,
                                0x93, 0x7C, 0x30, 0x63, 0xF5, 0x10, 0x2E, 0x61,
                                0xD0, 0xBC, 0xB4, 0xB6, 0x06, 0xAA, 0xF4, 0x23,
                                0x78, 0x6E, 0x3B, 0xAE, 0xBF, 0x7B, 0x4C, 0xC1};


#define BAUD_RATE 4800

static void Gps2Date(gpx_t *gpx) {
    long GpsDays, Mjd;
    long J, C, Y, M;

    GpsDays = gpx->week * 7 + (gpx->gpssec / 86400);
    Mjd = 44244 + GpsDays;

    J = Mjd + 2468570;
    C = 4 * J / 146097;
    J = J - (146097 * C + 3) / 4;
    Y = 4000 * (J + 1) / 1461001;
    J = J - 1461 * Y / 4 + 31;
    M = 80 * J / 2447;
    gpx->tag = J - 2447 * M / 80;
    J = M / 11;
    gpx->monat = M + 2 - (12 * J);
    gpx->jahr = 100 * (C - 49) + Y + J;
}

static int bits2byte(char bits[]) {
    int i, byteval=0, d=1;
    for (i = 0; i < 8; i++) {
        if      (bits[i] == 1)  byteval += d;
        else if (bits[i] == 0)  byteval += 0;
        else return 0x100;
        d <<= 1;
    }
    return byteval;
}


static ui32_t u4(ui8_t *bytes) {
    ui32_t val = 0;
    memcpy(&val, bytes, 4);
    return val;
}

static ui32_t u3(ui8_t *bytes) {
    int val24 = 0;
    val24 = bytes[0] | (bytes[1]<<8) | (bytes[2]<<16);
    return val24;
}

static int i3(ui8_t *bytes) {
    int val = 0,
        val24 = 0;
    val = bytes[0] | (bytes[1]<<8) | (bytes[2]<<16);
    val24 = val & 0xFFFFFF; if (val24 & 0x800000) val24 = val24 - 0x1000000;
    return val24;
}

static ui32_t u2(ui8_t *bytes) {
    return  bytes[0] | (bytes[1]<<8);
}

static int i2(ui8_t *bytes) {
    int val = bytes[0] | (bytes[1]<<8);
    if (val & 0x8000) val -= 0x10000;
    return val;
}


static int crc16(ui8_t data[], int len) {
    int crc16poly = 0x1021;
    int rem = 0xFFFF, i, j;
    int byte;


    for (i = 0; i < len; i++) {
        byte = data[i];
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

static int check_CRC(gpx_t *gpx, ui32_t pos, ui32_t pck) {
    ui32_t crclen = 0,
           crcdat = 0;
    if (((pck>>8) & 0xFF) != gpx->frame[pos]) return -1;
    crclen = gpx->frame[pos+1];
    if (pos + crclen + 4 > FRAME_LEN) return -1;
    crcdat = u2(gpx->frame+pos+2+crclen);
    if ( crcdat != crc16(gpx->frame+pos+2, crclen) ) {
        return 1;
    }
    else return 0;
}



#define crc_FRAME      (1<<0)
#define xor_FRAME      0x1713
#define pck_FRAME      0x7928
#define pos_FRAME       0x039
#define pos_FrameNb     0x03B
#define pos_BattVolts   0x045
#define pos_SondeID     0x03D
#define pos_TXpower     0x050
#define pos_CPUtemp     0x04B
#define pos_CalData     0x052
#define pos_Calfreq     0x055
#define pos_Calburst    0x05E
#define pos_CalRSTyp    0x05B
#define pos_CalRSTyp2   0x053
#define pos_CalRSM      0x055

#define crc_PTU        (1<<1)
#define xor_PTU        0xE388
#define pck_PTU        0x7A2A
#define pos_PTU         0x065

#define crc_GPS1       (1<<2)
#define xor_GPS1       0x9667
#define pck_GPS1       0x7C1E
#define pos_GPS1        0x093
#define pos_GPSweek     0x095
#define pos_GPSiTOW     0x097
#define pos_satsN       0x09B

#define crc_GPS2       (1<<3)
#define xor_GPS2       0xD7AD
#define pck_GPS2       0x7D59
#define pos_GPS2        0x0B5
#define pos_minPR       0x0B7
#define pos_FF          0x0BB
#define pos_dataSats    0x0BC

#define crc_GPS3       (1<<4)
#define xor_GPS3       0xB9FF
#define pck_GPS3       0x7B15
#define pos_GPS3        0x112
#define pos_GPSecefX    0x114
#define pos_GPSecefY    0x118
#define pos_GPSecefZ    0x11C
#define pos_GPSecefV    0x120
#define pos_numSats     0x126
#define pos_sAcc        0x127
#define pos_pDOP        0x128

#define crc_AUX        (1<<5)
#define pck_AUX        0x7E00
#define pos_AUX         0x12B

#define crc_ZERO       (1<<6)
#define pck_ZERO       0x7600
#define pck_ZEROstd    0x7611
#define pos_ZEROstd     0x12B

#define pck_SGM_xTU    0x7F1B
#define pck_SGM_CRYPT  0x80A7

#define pck_960A              0x960A
#define pck_8226_POSDATETIME  0x8226
#define pck_8329_SATS         0x8329


static int frametype(gpx_t *gpx) {
    int i;
    ui8_t b = gpx->frame[pos_FRAME-1];
    int ft = 0;
    for (i = 0; i < 4; i++) {
        ft += ((b>>i)&1) - ((b>>(i+4))&1);
    }
    return ft;
}

static int get_FrameNb(gpx_t *gpx, int crc, int ofs) {
    int i;
    unsigned byte;
    ui8_t frnr_bytes[2];
    int frnr;

    for (i = 0; i < 2; i++) {
        byte = gpx->frame[pos_FrameNb+ofs + i];
        frnr_bytes[i] = byte;
    }

    frnr = frnr_bytes[0] + (frnr_bytes[1] << 8);
    gpx->frnr = frnr;

    if (crc == 0) {
        gpx->ecdat.last_frnb = frnr;
        gpx->ecdat.last_frnb_ts = gpx->ecdat.ts;
    }

    return 0;
}

static int get_BattVolts(gpx_t *gpx, int ofs) {
    int i;
    unsigned byte;
    ui8_t batt_bytes[2];
    ui16_t batt_volts;

    for (i = 0; i < 2; i++) {
        byte = gpx->frame[pos_BattVolts+ofs + i];
        batt_bytes[i] = byte;
    }
    batt_volts = batt_bytes[0];
    gpx->batt = batt_volts/10.0;

    return 0;
}

static int get_TXpower(gpx_t *gpx, int ofs) {
    gpx->txpower = gpx->frame[pos_TXpower+ofs];
    gpx->txpower_valid = 1;
    return 0;
}

static int get_SondeID(gpx_t *gpx, int crc, int ofs) {
    int i;
    unsigned byte;
    char sondeid_bytes[9];

    if (crc == 0) {
        for (i = 0; i < 8; i++) {
            byte = gpx->frame[pos_SondeID+ofs + i];
            sondeid_bytes[i] = byte;
        }
        sondeid_bytes[8] = '\0';
        if ( strncmp(gpx->id, sondeid_bytes, 8) != 0 ) {
            memset(gpx->calfrchk, 0, 51);
            memset(gpx->rstyp, 0, 10);
            memset(gpx->rstmp, 0, 10);
            memset(gpx->rsm, 0, 10);
            gpx->calconf_complete = 0;
            gpx->calconf_sent = 0;
            gpx->freq = 0;
            gpx->conf_fw = 0;
            gpx->conf_bt = 0;
            gpx->conf_bk = 0;
            gpx->conf_cd = -1;
            gpx->conf_kt = -1;
            gpx->jahr = 0; gpx->monat = 0; gpx->tag = 0;
            gpx->std = 0; gpx->min = 0; gpx->sek = 0.0;
            gpx->week = 0;
            gpx->lat = 0.0; gpx->lon = 0.0; gpx->alt = 0.0;
            gpx->vH  = 0.0; gpx->vD  = 0.0; gpx->vV  = 0.0;
            gpx->numSV = 0;
            gpx->gnss_numSVb168 = 0;
            gpx->gnss_nSVstatus = 0;
            memset(gpx->gnss_sv, 0, 32*sizeof(gnss_t));
            gpx->isUTC = 0;
            gpx->T = -273.15f;
            gpx->RH = -1.0f;
            gpx->P = -1.0f;
            gpx->RH2 = -1.0f;
            memcpy(gpx->id, sondeid_bytes, 8);
            gpx->id[8] = '\0';

            gpx->ecdat.last_frnb = 0;
        }
    }

    return 0;
}

static int get_FrameConf(gpx_t *gpx, int ofs) {
    int crc, err;
    ui8_t calfr;
    int i;

    crc = check_CRC(gpx, pos_FRAME+ofs, pck_FRAME);
    if (crc) gpx->crc |= crc_FRAME;

    err = crc;
    err |= get_SondeID(gpx, crc, ofs);
    err |= get_FrameNb(gpx, crc, ofs);
    err |= get_BattVolts(gpx, ofs);
    err |= get_TXpower(gpx, ofs);

    if (crc == 0) {
        calfr = gpx->frame[pos_CalData+ofs];
        if (gpx->calfrchk[calfr] == 0)
        {
            for (i = 0; i < 16; i++) {
                gpx->calibytes[calfr*16 + i] = gpx->frame[pos_CalData+ofs+1+i];
            }
            gpx->calfrchk[calfr] = 1;
        }

        gpx->ecdat.last_calfrm = calfr;
        gpx->ecdat.last_calfrm_ts = gpx->ecdat.ts;

        if ( !gpx->calconf_complete ) {
            int sum = 0;
            for (i = 0; i < 51; i++) {
                sum += gpx->calfrchk[i];
            }
            if (sum == 51) {
                int calconf_dat = gpx->calibytes[0] | (gpx->calibytes[1]<<8);
                int calconf_crc = crc16(gpx->calibytes+2, 50*16-2);

                if (calconf_dat == calconf_crc) gpx->calconf_complete = 1;
            }
        }
    }

    return err;
}

static int get_CalData(gpx_t *gpx) {

    int j;

    memcpy(&(gpx->ptu_Rf1), gpx->calibytes+61, 4);
    memcpy(&(gpx->ptu_Rf2), gpx->calibytes+65, 4);

    memcpy(gpx->ptu_co1+0, gpx->calibytes+77, 4);
    memcpy(gpx->ptu_co1+1, gpx->calibytes+81, 4);
    memcpy(gpx->ptu_co1+2, gpx->calibytes+85, 4);

    memcpy(gpx->ptu_calT1+0, gpx->calibytes+89, 4);
    memcpy(gpx->ptu_calT1+1, gpx->calibytes+93, 4);
    memcpy(gpx->ptu_calT1+2, gpx->calibytes+97, 4);

    memcpy(gpx->ptu_calH+0, gpx->calibytes+117, 4);
    memcpy(gpx->ptu_calH+1, gpx->calibytes+121, 4);

    memcpy(gpx->ptu_co2+0, gpx->calibytes+293, 4);
    memcpy(gpx->ptu_co2+1, gpx->calibytes+297, 4);
    memcpy(gpx->ptu_co2+2, gpx->calibytes+301, 4);

    memcpy(gpx->ptu_calT2+0, gpx->calibytes+305, 4);
    memcpy(gpx->ptu_calT2+1, gpx->calibytes+309, 4);
    memcpy(gpx->ptu_calT2+2, gpx->calibytes+313, 4);


    memcpy(&(gpx->ptu_Cf1), gpx->calibytes+69, 4);
    memcpy(&(gpx->ptu_Cf2), gpx->calibytes+73, 4);
    for (j = 0; j < 42; j++) {
        memcpy(gpx->ptu_mtxH+j, gpx->calibytes+125+4*j, 4);
    }
    for (j = 0; j <  3; j++) {
        memcpy(gpx->ptu_corHp+j, gpx->calibytes+678+4*j, 4);
    }
    for (j = 0; j < 12; j++) {
        memcpy(gpx->ptu_corHt+j, gpx->calibytes+698+4*j, 4);
    }
    memcpy(gpx->ptu_calP+0,  gpx->calibytes+606, 4);
    memcpy(gpx->ptu_calP+4,  gpx->calibytes+610, 4);
    memcpy(gpx->ptu_calP+8,  gpx->calibytes+614, 4);
    memcpy(gpx->ptu_calP+12, gpx->calibytes+618, 4);
    memcpy(gpx->ptu_calP+16, gpx->calibytes+622, 4);
    memcpy(gpx->ptu_calP+20, gpx->calibytes+626, 4);
    memcpy(gpx->ptu_calP+24, gpx->calibytes+630, 4);
    memcpy(gpx->ptu_calP+1,  gpx->calibytes+634, 4);
    memcpy(gpx->ptu_calP+5,  gpx->calibytes+638, 4);
    memcpy(gpx->ptu_calP+9,  gpx->calibytes+642, 4);
    memcpy(gpx->ptu_calP+13, gpx->calibytes+646, 4);
    memcpy(gpx->ptu_calP+2,  gpx->calibytes+650, 4);
    memcpy(gpx->ptu_calP+6,  gpx->calibytes+654, 4);
    memcpy(gpx->ptu_calP+10, gpx->calibytes+658, 4);
    memcpy(gpx->ptu_calP+14, gpx->calibytes+662, 4);
    memcpy(gpx->ptu_calP+3,  gpx->calibytes+666, 4);
    memcpy(gpx->ptu_calP+7,  gpx->calibytes+670, 4);
    memcpy(gpx->ptu_calP+11, gpx->calibytes+674, 4);

    return 0;
}

static float get_T(gpx_t *gpx, ui32_t f, ui32_t f1, ui32_t f2, float *ptu_co, float *ptu_calT) {
    float *p = ptu_co;
    float *c = ptu_calT;
    float  g = (float)(f2-f1)/(gpx->ptu_Rf2-gpx->ptu_Rf1),
          Rb = (f1*gpx->ptu_Rf2-f2*gpx->ptu_Rf1)/(float)(f2-f1),
          Rc = f/g - Rb,
          R = Rc * c[0],
          T = (p[0] + p[1]*R + p[2]*R*R + c[1])*(1.0 + c[2]);
    return T;
}

static float get_RHemp(gpx_t *gpx, ui32_t f, ui32_t f1, ui32_t f2, float T) {
    float a0 = 7.5;
    float a1 = 350.0/gpx->ptu_calH[0];
    float fh = (f-f1)/(float)(f2-f1);
    float rh = 100.0 * ( a1*fh - a0 );
    float T0 = 0.0, T1 = -20.0, T2 = -40.0;
    rh += T0 - T/5.5;
    if (T < T1) rh *= 1.0 + (T1-T)/100.0;
    if (T < T2) rh *= 1.0 + (T2-T)/120.0;
    if (rh < 0.0) rh = 0.0;
    if (rh > 100.0) rh = 100.0;
    if (T < -273.0) rh = -1.0;
    return rh;
}

static float vaporSatP(float Tc) {
    double T = Tc + 273.15;


    double p = expf(-5800.2206 / T
                    +1.3914993
                    +6.5459673 * log(T)
                    -4.8640239e-2 * T
                    +4.1764768e-5 * T*T
                    -1.4452093e-8 * T*T*T
                   );

    return (float)p;
}
static float get_RH2adv(gpx_t *gpx, ui32_t f, ui32_t f1, ui32_t f2, float T, float TH, float P) {
    float rh  = 0.0;
    float cfh = (f-f1)/(float)(f2-f1);
    float cap = gpx->ptu_Cf1+(gpx->ptu_Cf2-gpx->ptu_Cf1)*cfh;
    double Cp = (cap / gpx->ptu_calH[0] - 1.0) * gpx->ptu_calH[1];
    double Trh_20_180 = (TH - 20.0) / 180.0;
    double _rh = 0.0;
    double aj = 1.0;
    double bk = 1.0, b[6];
    int j, k;

    bk = 1.0;
    for (k = 0; k < 6; k++) {
        b[k] = bk;
        bk *= Trh_20_180;
    }

    if (P > 0.0)
    {
        double _p = P / 1000.0;
        double _cpj = 1.0;
        double corrCp = 0.0;
        double bt, bp[3];

        for (j = 0; j < 3; j++) {
            bp[j] = gpx->ptu_corHp[j] * (_p/(1.0 + gpx->ptu_corHp[j]*_p) - _cpj/(1.0 + gpx->ptu_corHp[j]));
            _cpj *= Cp;
        }

        corrCp = 0.0;
        for (j = 0; j < 3; j++) {
            bt = 0.0;
            for (k = 0; k < 4; k++) {
                bt += gpx->ptu_corHt[4*j+k] * b[k];
            }
            corrCp += bp[j] * bt;
        }
        Cp -= corrCp;
    }

    aj = 1.0;
    for (j = 0; j < 7; j++) {
        for (k = 0; k < 6; k++) {
            _rh += aj * b[k] * gpx->ptu_mtxH[6*j+k];
        }
        aj *= Cp;
    }

    if ( P <= 0.0 ) {
        float T2 = -40;
        if (T < T2) { _rh += (T-T2)/12.0; }
    }

    rh = _rh * vaporSatP(TH)/vaporSatP(T);

    if (rh < 0.0) rh = 0.0;
    if (rh > 100.0) rh = 100.0;
    return rh;
}
static float get_P(gpx_t *gpx, ui32_t f, ui32_t f1, ui32_t f2, int fx)
{
    double p = 0.0;
    double a0, a1, a0j, a1k;
    int j, k;
    if (f1 == f2 || f1 == f) return 0.0;
    a0 = gpx->ptu_calP[24] / ((float)(f - f1) / (float)(f2 - f1));
    a1 = fx * 0.01;

    a0j = 1.0;
    for (j = 0; j < 6; j++) {
        a1k = 1.0;
        for (k = 0; k < 4; k++) {
            p += a0j * a1k * gpx->ptu_calP[j*4+k];
            a1k *= a1;
        }
        a0j *= a0;
    }

    return (float)p;
}
static float Ph(float h) {
    double Pb, Tb, Lb, hb;
	double gMR = 9.80665*0.0289644/8.31446;
	float P = 0.0;

    if (h > 32000.0) {
        Pb = 8.6802;
        Tb = 228.65;
        Lb = 0.0028;
        hb = 32000.0;
    }
    else if (h > 20000.0) {
        Pb = 54.7489;
        Tb = 216.65;
        Lb = 0.001;
        hb = 20000.0;
    }
    else if (h > 11000.0) {
        Pb = 226.321;
        Tb = 216.65;
        Lb = 0.0;
        hb = 11000.0;
    }
    else {
        Pb = 1013.25;
        Tb = 288.15;
        Lb = -0.0065;
        hb = 0.0;
    }

    if (Lb == 0.0) P = Pb * exp( -gMR*(h-hb)/Tb );
    else           P = Pb * pow( 1.0+Lb*(h-hb)/Tb , -gMR/Lb);

    return P;
}

static int get_PTU(gpx_t *gpx, int ofs, int pck, int valid_alt) {
    int err=0, i;
    int bR, bc1, bT1,
            bc2, bT2;
    int bH;
    int bH2;
    int bP;
    ui32_t meas[12];
    float Tc = -273.15;
    float TH = -273.15;
    float RH = -1.0;
    float RH2 = -1.0;
    float P = -1.0;

    get_CalData(gpx);

    err = check_CRC(gpx, pos_PTU+ofs, pck);
    if (err) gpx->crc |= crc_PTU;

    if (err == 0)
    {
        for (i = 0; i < 12; i++) {
            meas[i] = u3(gpx->frame+pos_PTU+ofs+2+3*i);
        }

        bR  = gpx->calfrchk[0x03] && gpx->calfrchk[0x04];
        bc1 = gpx->calfrchk[0x04] && gpx->calfrchk[0x05];
        bT1 = gpx->calfrchk[0x05] && gpx->calfrchk[0x06];
        bc2 = gpx->calfrchk[0x12] && gpx->calfrchk[0x13];
        bT2 = gpx->calfrchk[0x13];
        bH  = gpx->calfrchk[0x07];

        bH2 = gpx->calfrchk[0x07] && gpx->calfrchk[0x08]
           && gpx->calfrchk[0x09] && gpx->calfrchk[0x0A]
           && gpx->calfrchk[0x0B] && gpx->calfrchk[0x0C]
           && gpx->calfrchk[0x0D] && gpx->calfrchk[0x0E]
           && gpx->calfrchk[0x0F] && gpx->calfrchk[0x10]
           && gpx->calfrchk[0x11] && gpx->calfrchk[0x12]
           && gpx->calfrchk[0x2A] && gpx->calfrchk[0x2B]
           && gpx->calfrchk[0x2C] && gpx->calfrchk[0x2D]
           && gpx->calfrchk[0x2E];

        bP  = gpx->calfrchk[0x21] && gpx->calibytes[0x21F] == 'P'
           && gpx->calfrchk[0x25] && gpx->calfrchk[0x26]
           && gpx->calfrchk[0x27] && gpx->calfrchk[0x28]
           && gpx->calfrchk[0x29] && gpx->calfrchk[0x2A];

        if (bR && bc1 && bT1) {
            Tc = get_T(gpx, meas[0], meas[1], meas[2], gpx->ptu_co1, gpx->ptu_calT1);
        }
        gpx->T = Tc;

        if (bR && bc2 && bT2) {
            TH = get_T(gpx, meas[6], meas[7], meas[8], gpx->ptu_co2, gpx->ptu_calT2);
        }
        gpx->TH = TH;

        if (bH && Tc > -273.0) {
            RH = get_RHemp(gpx, meas[3], meas[4], meas[5], Tc);
        }
        gpx->RH = RH;

        if (bP) {
            P = get_P(gpx, meas[9], meas[10], meas[11], i2(gpx->frame+pos_PTU+ofs+2+38));
        }
        gpx->P = P;
        if (gpx->option.ptu == 2) {
            float _P = -1.0;
            if (bP) _P = P;
            else {
                if (valid_alt > 0) _P = Ph(gpx->alt);
            }
            if (bH && bH2 && Tc > -273.0 && TH > -273.0) {
                RH2 = get_RH2adv(gpx, meas[3], meas[4], meas[5], Tc, TH, _P);
            }
        }
        gpx->RH2 = RH2;


        if (gpx->option.vbs == 4 && (gpx->crc & (crc_PTU | crc_GPS3))==0)
        {
            printf("  h: %8.2f   # ", gpx->alt);

            printf("1: %8d %8d %8d", meas[0], meas[1], meas[2]);
            printf("   #   ");
            printf("2: %8d %8d %8d", meas[3], meas[4], meas[5]);
            printf("   #   ");
            printf("3: %8d %8d %8d", meas[6], meas[7], meas[8]);
            printf("   #   ");

            if (0 && Tc > -273.0 && RH > -0.5)
            {
                printf("  ");
                printf(" Tc:%.2f ", Tc);
                printf(" RH:%.1f ", RH);
                printf(" TH:%.2f ", TH);
            }
            printf("\n");

            {
                printf("    %9.2f ; %6.1f ; %6.1f ", gpx->alt, gpx->ptu_Rf1, gpx->ptu_Rf2);
                printf("; %10.6f ; %10.6f ; %10.6f ", gpx->ptu_calT1[0], gpx->ptu_calT1[1], gpx->ptu_calT1[2]);
                printf("; %10.6f ; %10.6f ", gpx->ptu_calH[0], gpx->ptu_calH[1]);
                printf("; %10.6f ; %10.6f ; %10.6f ", gpx->ptu_calT2[0], gpx->ptu_calT2[1], gpx->ptu_calT2[2]);
                printf("\n");
            }
        }

    }

    return err;
}


static int get_GPSweek(gpx_t *gpx, int ofs) {
    int i;
    unsigned byte;
    ui8_t gpsweek_bytes[2];
    int gpsweek;

    for (i = 0; i < 2; i++) {
        byte = gpx->frame[pos_GPSweek+ofs + i];
        gpsweek_bytes[i] = byte;
    }

    gpsweek = gpsweek_bytes[0] + (gpsweek_bytes[1] << 8);
    gpx->week = gpsweek;

    return 0;
}

static char weekday[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static int get_GPStime(gpx_t *gpx, int ofs) {
    int i;
    unsigned byte;
    ui8_t gpstime_bytes[4];
    int gpstime = 0,
        day;
    int ms;

    for (i = 0; i < 4; i++) {
        byte = gpx->frame[pos_GPSiTOW+ofs + i];
        gpstime_bytes[i] = byte;
    }

    memcpy(&gpstime, gpstime_bytes, 4);

    gpx->tow_ms = gpstime;
    ms = gpstime % 1000;
    gpstime /= 1000;
    gpx->gpssec = gpstime;

    day = (gpstime / (24 * 3600)) % 7;

    gpstime %= (24*3600);

    gpx->wday = day;
    gpx->std =  gpstime / 3600;
    gpx->min = (gpstime % 3600) / 60;
    gpx->sek =  gpstime % 60 + ms/1000.0;
    gpx->isUTC = 0;

    return 0;
}

static int get_GPS1(gpx_t *gpx, int ofs) {
    int err=0;

    err = check_CRC(gpx, pos_GPS1+ofs, pck_GPS1);
    if (err) {
        gpx->crc |= crc_GPS1;
        gpx->jahr = 0; gpx->monat = 0; gpx->tag = 0;
        gpx->std = 0; gpx->min = 0; gpx->sek = 0.0;
        gpx->isUTC = 0;
        return -1;
    }

    err |= get_GPSweek(gpx, ofs);
    err |= get_GPStime(gpx, ofs);

    return err;
}

static int get_GPS2(gpx_t *gpx, int ofs) {
    int err=0;

    err = check_CRC(gpx, pos_GPS2+ofs, pck_GPS2);
    if (err) gpx->crc |= crc_GPS2;

    return err;
}

#define EARTH_a  6378137.0
#define EARTH_b  6356752.31424518
#define EARTH_a2_b2  (EARTH_a*EARTH_a - EARTH_b*EARTH_b)

const
double a = EARTH_a,
       b = EARTH_b,
       a_b = EARTH_a2_b2,
       e2  = EARTH_a2_b2 / (EARTH_a*EARTH_a),
       ee2 = EARTH_a2_b2 / (EARTH_b*EARTH_b);

static void ecef2elli(double X[], double *lat, double *lon, double *alt) {
    double phi, lam, R, p, t;

    lam = atan2( X[1] , X[0] );

    p = sqrt( X[0]*X[0] + X[1]*X[1] );
    t = atan2( X[2]*a , p*b );

    phi = atan2( X[2] + ee2 * b * sin(t)*sin(t)*sin(t) ,
                 p - e2 * a * cos(t)*cos(t)*cos(t) );

    R = a / sqrt( 1 - e2*sin(phi)*sin(phi) );
    *alt = p / cos(phi) - R;

    *lat = phi*180/M_PI;
    *lon = lam*180/M_PI;
}

static int get_ECEFkoord(gpx_t *gpx, int pos_ecef) {
    int i, k;
    unsigned byte;
    ui8_t XYZ_bytes[4];
    int XYZ;
    double X[3], lat, lon, alt;
    ui8_t gpsVel_bytes[2];
    short vel16;
    double V[3];
    double phi, lam, dir;
    double vN; double vE; double vU;


    for (k = 0; k < 3; k++) {

        for (i = 0; i < 4; i++) {
            byte = gpx->frame[pos_ecef + 4*k + i];
            XYZ_bytes[i] = byte;
        }
        memcpy(&XYZ, XYZ_bytes, 4);
        X[k] = XYZ / 100.0;

        for (i = 0; i < 2; i++) {
            byte = gpx->frame[pos_ecef+12 + 2*k + i];
            gpsVel_bytes[i] = byte;
        }
        vel16 = gpsVel_bytes[0] | gpsVel_bytes[1] << 8;
        V[k] = vel16 / 100.0;

    }


    ecef2elli(X, &lat, &lon, &alt);
    gpx->lat = lat;
    gpx->lon = lon;
    gpx->alt = alt;
    if ((alt < -1000) || (alt > 80000)) return -3;


    phi = lat*M_PI/180.0;
    lam = lon*M_PI/180.0;
    vN = -V[0]*sin(phi)*cos(lam) - V[1]*sin(phi)*sin(lam) + V[2]*cos(phi);
    vE = -V[0]*sin(lam) + V[1]*cos(lam);
    vU =  V[0]*cos(phi)*cos(lam) + V[1]*cos(phi)*sin(lam) + V[2]*sin(phi);

    gpx->vH = sqrt(vN*vN+vE*vE);
    dir = atan2(vE, vN) * 180 / M_PI;
    if (dir < 0) dir += 360;
    gpx->vD = dir;

    gpx->vV = vU;

    return 0;
}

static int get_GPS3(gpx_t *gpx, int ofs) {
    int err=0;

    err = check_CRC(gpx, pos_GPS3+ofs, pck_GPS3);
    if (err) {
        gpx->crc |= crc_GPS3;
        gpx->lat = 0.0; gpx->lon = 0.0; gpx->alt = 0.0;
        gpx->vH  = 0.0; gpx->vD  = 0.0; gpx->vV  = 0.0;
        gpx->numSV = 0;
        return -1;
    }
    err |= get_ECEFkoord(gpx, pos_GPS3+ofs+2);

    gpx->numSV = gpx->frame[pos_numSats+ofs];

    return err;
}

static int get_posdatetime(gpx_t *gpx, int pos_posdatetime) {
    int err=0;

    err = check_CRC(gpx, pos_posdatetime, pck_8226_POSDATETIME);
    if (err) {
        gpx->crc |= crc_GPS1 | crc_GPS3;
        gpx->jahr = 0; gpx->monat = 0; gpx->tag = 0;
        gpx->std = 0; gpx->min = 0; gpx->sek = 0.0;
        gpx->isUTC = 0;
        gpx->lat = 0.0; gpx->lon = 0.0; gpx->alt = 0.0;
        gpx->vH  = 0.0; gpx->vD  = 0.0; gpx->vV  = 0.0;
        gpx->numSV = 0;
        return -1;
    }

    err |= get_ECEFkoord(gpx, pos_posdatetime+2);

    gpx->jahr  = gpx->frame[pos_posdatetime+20] | gpx->frame[pos_posdatetime+21]<<8;
    gpx->monat = gpx->frame[pos_posdatetime+22];
    gpx->tag   = gpx->frame[pos_posdatetime+23];
    gpx->std = gpx->frame[pos_posdatetime+24];
    gpx->min = gpx->frame[pos_posdatetime+25];
    gpx->sek = gpx->frame[pos_posdatetime+26];
    if (gpx->frame[pos_posdatetime+27] < 100) gpx->sek += gpx->frame[pos_posdatetime+27]/100.0;
    gpx->isUTC = 1;


    return err;
}

static int get_gnssSVs(gpx_t *gpx, int pos_gnss2) {
    int err=0;

    memset(gpx->gnss_sv, 0, 32*sizeof(gnss_t));
    gpx->gnss_numSVb168 = 0;
    gpx->gnss_nSVstatus = 0;

    err = check_CRC(gpx, pos_gnss2, pck_8329_SATS);

    if (!err) {


        int cntSV168 = 0;
        for (int j = 0; j < 21; j++) {
            int b = gpx->frame[pos_gnss2+2+4+j];
            for (int n = 0; n < 8; n++) {
                int s = (b>>n)&1;
                if (s) {
                    ui8_t svid = j*8+n + 1;
                    if (cntSV168 < 32) {
                        gpx->gnss_sv[cntSV168].id168 = svid;
                    }
                    cntSV168 += 1;
                }
            }
        }
        gpx->gnss_numSVb168 = cntSV168;

        int cntSVstatus = 0;
        for (int j = 0; j < 16; j++) {
            ui8_t b = gpx->frame[pos_gnss2+2+4+21+j];
            ui8_t b0 = b & 0xF;
            ui8_t b1 = (b>>4) & 0xF;
            gpx->gnss_sv[2*j  ].status = b0; if (b0) cntSVstatus++;
            gpx->gnss_sv[2*j+1].status = b1; if (b1) cntSVstatus++;
        }
        gpx->gnss_nSVstatus = cntSVstatus;


        gpx->numSV = gpx->gnss_nSVstatus;
    }
    else {
        gpx->crc |= crc_GPS2;
    }

    return err;
}

static int prn_gnss_sat2(gpx_t *gpx) {
    int n;

    fprintf(stdout, "\n");
    fprintf(stdout, "  numSV168 : %2d", gpx->gnss_numSVb168);
    fprintf(stdout, "  nSVstatus: %2d", gpx->gnss_nSVstatus);
    fprintf(stdout, "\n");
    fprintf(stdout, "  SVids: ");
    for (n = 0; n < 32; n++) {
        if (n < gpx->gnss_numSVb168) fprintf(stdout, " %3d", gpx->gnss_sv[n].id168);
        if (n < gpx->gnss_nSVstatus) fprintf(stdout, ":%X", gpx->gnss_sv[n].status);
    }
    fprintf(stdout, "\n");

    for (n = 0; n < 32; n++) {
        if (n < gpx->gnss_numSVb168 || n < gpx->gnss_nSVstatus) {
            if (gpx->gnss_sv[n].id168 < 33) {
                ui8_t prnGPS = gpx->gnss_sv[n].id168;
                if (n == 0 && gpx->gnss_sv[n].id168 < 33) fprintf(stdout, "  GPS: ");
                fprintf(stdout, " PRN%02d", prnGPS);
            }
            else if (gpx->gnss_sv[n].id168 < 33+36) {
                ui8_t prnGAL = gpx->gnss_sv[n].id168 - 32;
                if (n == 0 || n > 0 && gpx->gnss_sv[n-1].id168 < 33) {
                    if (n > 0) fprintf(stdout, "\n");
                    fprintf(stdout, "  GAL: ");
                }
                fprintf(stdout, " E%02d", prnGAL);
            }
        }
    }
    fprintf(stdout, "\n");

    return 0;
}


static int hex2uint(char *str, int nibs) {
    int i;
    int erg = 0;
    int h = 0;

    if (nibs > 7) return -2;

    for (i = 0; i < nibs; i++) {
        if      (str[i] >= '0' && str[i] <= '9') h = str[i]-'0';
        else if (str[i] >= 'a' && str[i] <= 'f') h = str[i]-'a'+0xA;
        else if (str[i] >= 'A' && str[i] <= 'F') h = str[i]-'A'+0xA;
        else return -1;
        erg = (erg << 4) | (h & 0xF);
    }
    return erg;
}

static int prn_aux_IDx01(char *xdata) {
    int val;
    i16_t Tpump;
    ui16_t Icell;
    ui8_t InstrNum, Ipump, Vbat;
    char *px = xdata;
    int N = 2*8;

    if (*px) {

        if (strncmp(px, "01", 2) != 0) {
            px = strstr(xdata, "#01");
            if (px == NULL) return -1;
            else px += 1;
        }
        if (strlen(px) < N) return -1;

        fprintf(stdout, " ID=0x01 ECC ");
        val = hex2uint(px+ 2, 2);  if (val < 0) return -1;
        InstrNum = val & 0xFF;
        val = hex2uint(px+ 4, 4);  if (val < 0) return -1;
        Icell = val & 0xFFFF;
        val = hex2uint(px+ 8, 4);  if (val < 0) return -1;
        Tpump = val & 0xFFFF;
        val = hex2uint(px+12, 2);  if (val < 0) return -1;
        Ipump = val & 0xFF;
        val = hex2uint(px+14, 2);  if (val < 0) return -1;
        Vbat  = val & 0xFF;
        fprintf(stdout, " No.%d ", InstrNum);
        fprintf(stdout, " Icell:%.3fuA ", Icell/1000.0);
        fprintf(stdout, " Tpump:%.2fC ", Tpump/100.0);
        fprintf(stdout, " Ipump:%dmA ", Ipump);
        fprintf(stdout, " Vbat:%.1fV ", Vbat/10.0);
    }
    else {
        return -2;
    }

    return 0;
}

static int prn_aux_IDx05(char *xdata) {
    char *px = xdata;
    int N = 2*10;
    int val;
    ui8_t InstrNum;

    if (*px) {

        if (strncmp(px, "05", 2) != 0) {
            px = strstr(xdata, "#05");
            if (px == NULL) return -1;
            else px += 1;
        }
        if (strlen(px) < N) return -1;

        fprintf(stdout, " ID=0x05 OIF411 ");
        val = hex2uint(px+ 2, 2);  if (val < 0) return -1;
        InstrNum = val & 0xFF;
        fprintf(stdout, " No.%d ", InstrNum);

        if (px[N] == 'I') {
            ui16_t dw;
            ui16_t sw;
            char sn[9];
            N += 1;
            strncpy(sn, px+4, 8); sn[8] = '\0';
            val = hex2uint(px+12, 4);  if (val < 0) return -1;
            dw = val & 0xFFFF;
            val = hex2uint(px+16, 4);  if (val < 0) return -1;
            sw = val & 0xFFFF;
            fprintf(stdout, " SN:%s ", sn);
            fprintf(stdout, " DW:%04X ", dw);
            fprintf(stdout, " SW:%.2f ", sw/100.0);
        }
        else {
            ui32_t Icell;
            ui16_t Ipump;
            i16_t Tpump;
            ui8_t InstrNum, Vbat, Vext;
            val = hex2uint(px+ 4, 4);  if (val < 0) return -1;
            Tpump = val & 0xFFFF;
            val = hex2uint(px+ 8, 5);  if (val < 0) return -1;
            Icell = val & 0xFFFFF;
            val = hex2uint(px+13, 2);  if (val < 0) return -1;
            Vbat  = val & 0xFF;
            val = hex2uint(px+15, 3);  if (val < 0) return -1;
            Ipump = val & 0xFFF;
            val = hex2uint(px+18, 2);  if (val < 0) return -1;
            Vext  = val & 0xFF;
            fprintf(stdout, " Tpump:%.2fC ", Tpump/100.0);
            fprintf(stdout, " Icell:%.4fuA ", Icell/10000.0);
            fprintf(stdout, " Vbat:%.1fV ", Vbat/10.0);
            fprintf(stdout, " Ipump:%dmA ", Ipump);
            fprintf(stdout, " Vext:%.1fV ", Vext/10.0);
        }
    }
    else {
        return -2;
    }

    return 0;
}

static int prn_aux_IDx08(char *xdata) {
    char *px = xdata;
    int N = 2*12;
    int val;
    ui8_t InstrNum;

    if (*px) {

        if (strncmp(px, "08", 2) != 0) {
            px = strstr(xdata, "#08");
            if (px == NULL) return -1;
            else px += 1;
        }
        if (strlen(px) < N) return -1;

        fprintf(stdout, " ID=0x08 CFH ");
        val = hex2uint(px+ 2, 2);  if (val < 0) return -1;
        InstrNum = val & 0xFF;
        fprintf(stdout, " No.%d ", InstrNum);
        fprintf(stdout, " Tmir:0x%.6s ", px+4);
        fprintf(stdout, " Vopt:0x%.6s ", px+10);
        fprintf(stdout, " Topt:0x%.4s ", px+16);
        fprintf(stdout, " Vbat:0x%.4s ", px+20);

    }
    else {
        return -2;
    }

    return 0;
}

static int get_Aux(gpx_t *gpx, int out, int pos) {
    int auxlen, auxcrc, count7E, pos7E;
    int i, n;
    char *paux;

    n = 0;
    count7E = 0;
    pos7E = 0;
    gpx->xdata[0] = '\0';

    if (frametype(gpx) <= 0)
    {
        while ( pos < FRAME_LEN  &&  gpx->frame[pos] == 0x7E ) {

            auxlen = gpx->frame[pos+1];
            auxcrc = gpx->frame[pos+2+auxlen] | (gpx->frame[pos+2+auxlen+1]<<8);

            if ( pos + auxlen + 4 <= FRAME_LEN && auxcrc == crc16(gpx->frame+pos+2, auxlen) ) {
                if (count7E == 0) {
                    if (out) fprintf(stdout, "\n # xdata = ");
                }
                else {
                    if (out) fprintf(stdout, " # ");
                    gpx->xdata[n++] = '#';
                }

                for (i = 1; i < auxlen; i++) {
                    ui8_t c = gpx->frame[pos+2+i];
                    if (c > 0x1E && c < 0x7F) {
                        if (out) fprintf(stdout, "%c", c);
                        gpx->xdata[n++] = c;
                    }
                }
                count7E++;
                pos7E = pos;
                pos += 2+auxlen+2;
            }
            else {
                pos = FRAME_LEN;
                gpx->crc |= crc_AUX;
            }
        }
    }
    gpx->xdata[n] = '\0';

    paux = gpx->xdata;
    if (out && gpx->option.aux && *paux)
    {
        int val;
        ui8_t ID;
        for (i = 0; i < count7E; i++) {
            if (paux > gpx->xdata) {
                while (*paux && *paux != '#') paux++;
                paux++;
            }
            if (strlen(paux) > 2) {
                val = hex2uint(paux, 2);
                if (val < 0) { paux += 2; continue; }
                ID = val & 0xFF;
                switch (ID) {
                    case 0x01: fprintf(stdout, "\n"); prn_aux_IDx01(paux); break;
                    case 0x05: fprintf(stdout, "\n"); prn_aux_IDx05(paux); break;
                    case 0x08: fprintf(stdout, "\n"); prn_aux_IDx08(paux); break;
                }
                paux++;
            }
            else break;
        }
        if ( !gpx->option.jsn ) fprintf(stdout, "\n");
    }

    i = check_CRC(gpx, pos, pck_ZERO);
    if (i) gpx->crc |= crc_ZERO;

    return pos7E;
}

static int get_Calconf(gpx_t *gpx, int out, int ofs) {
    int i;
    unsigned byte;
    ui8_t calfr = 0;
    ui16_t fw = 0;
    int freq = 0, f0 = 0, f1 = 0;
    int err = 0;

    gpx->calconf_subfrm = gpx->frame+pos_CalData+ofs;

    byte = gpx->frame[pos_CalData+ofs];
    calfr = byte;
    err = check_CRC(gpx, pos_FRAME+ofs, pck_FRAME);

    if (out && gpx->option.vbs == 3) {
        fprintf(stdout, "\n");
        fprintf(stdout, "[%5d] ", gpx->frnr);
        fprintf(stdout, " 0x%02x: ", calfr);
        for (i = 0; i < 16; i++) {
            byte = gpx->frame[pos_CalData+ofs+1+i];
            fprintf(stdout, "%02x ", byte);
        }
        fprintf(stdout, " ");
    }

    if (err == 0)
    {
        if (calfr == 0x00) {
            byte = gpx->frame[pos_Calfreq+ofs] & 0xC0;
            f0 = (byte * 10) / 64;
            byte = gpx->frame[pos_Calfreq+ofs+1];
            f1 = 40 * byte;
            freq = 400000 + f1+f0;
            if (out && gpx->option.vbs) fprintf(stdout, ": fq %d ", freq);
            gpx->freq = freq;
        }

        if (calfr == 0x01) {
            fw = gpx->frame[pos_CalData+ofs+6] | (gpx->frame[pos_CalData+ofs+7]<<8);
            if (out && gpx->option.vbs) fprintf(stdout, ": fw 0x%04x ", fw);
            gpx->conf_fw = fw;
        }

        if (calfr == 0x02) {
            ui8_t  bk = gpx->frame[pos_Calburst+ofs];
            ui16_t kt = gpx->frame[pos_CalData+ofs+8] + (gpx->frame[pos_CalData+ofs+9] << 8);
            if (out && gpx->option.vbs) fprintf(stdout, ": BK %02X ", bk);
            if (out && gpx->option.vbs && kt != 0xFFFF ) fprintf(stdout, ": kt %.1fmin ", kt/60.0);
            gpx->conf_bk = bk;
            gpx->conf_kt = kt;
        }

        if (calfr == 0x31) {
            ui16_t bt = gpx->frame[pos_CalData+ofs+7] + (gpx->frame[pos_CalData+ofs+8] << 8);
            if (out  && bt != 0x0000 &&
                    (gpx->option.vbs == 3  ||  gpx->option.vbs && gpx->conf_bk)
               ) fprintf(stdout, ": bt %.1fmin ", bt/60.0);
            gpx->conf_bt = bt;
        }

        if (calfr == 0x32) {
            ui16_t cd = gpx->frame[pos_CalData+ofs+1] + (gpx->frame[pos_CalData+ofs+2] << 8);
            if (out && cd != 0xFFFF &&
                    (gpx->option.vbs == 3  ||  gpx->option.vbs && (gpx->conf_bk || gpx->conf_kt != 0xFFFF))
               ) fprintf(stdout, ": cd %.1fmin ", cd/60.0);
            gpx->conf_cd = cd;
        }

        if (calfr == 0x21) {
            memset(gpx->rstmp, 0, 10);
            for (i = 0; i < 8; i++) {
                byte = gpx->frame[pos_CalRSTyp+ofs + i];
                if ((byte >= 0x20) && (byte < 0x7F)) gpx->rstmp[i] = byte;
                else if (byte == 0x00) gpx->rstmp[i] = '\0';
            }

            if (out && gpx->option.vbs == 3) {
                float qfe1 = 0.0, qfe2 = 0.0;
                memcpy(&qfe1, gpx->frame+pos_CalData+ofs+1, 4);
                memcpy(&qfe2, gpx->frame+pos_CalData+ofs+5, 4);
                if (qfe1 > 0.0 || qfe2 > 0.0) {
                    fprintf(stdout, ": ");
                    if (qfe1 > 0.0) fprintf(stdout, "QFE1:%.1fhPa ", qfe1);
                    if (qfe2 > 0.0) fprintf(stdout, "QFE2:%.1fhPa ", qfe2);
                }
            }
        }

        if (calfr == 0x22) {
            byte = gpx->frame[pos_CalRSTyp2+ofs + 0];
            if ((byte >= 0x20) && (byte < 0x7F)) gpx->rstmp[8] = byte;
            else if (byte == 0x00) gpx->rstmp[8] = '\0';
            strcpy(gpx->rstyp, gpx->rstmp);
            memset(gpx->rstmp, 0, 10);
            if (out && gpx->option.vbs && *gpx->rstyp) fprintf(stdout, ": %s ", gpx->rstyp);

            memset(gpx->rsm, 0, 10);
            for (i = 0; i < 8; i++) {
                byte = gpx->frame[pos_CalRSM+ofs + i];
                if ((byte >= 0x20) && (byte < 0x7F)) gpx->rsm[i] = byte;
                else   gpx->rsm[i] = '\0';
            }
            if (out && gpx->option.vbs) fprintf(stdout, ": %s ", gpx->rsm);
        }
    }

    return 0;
}


static int set_bytes(gpx_t *gpx, int pos, ui8_t *src, int len, int subcw, int *pset) {
    int rem = 0;
    int i;
    if (subcw == 2) rem = 1; else rem = 0;
    for (i = 0; i < len; i++) {
        if ( (pos+i) % 2 == rem) {
            gpx->frame[pos+i] = src[i];
            *pset = pos+i;
            pset++;
        }
    }
    return 0;
}

#define N_idx_fixed 5
static int idx_fixed[N_idx_fixed] = { pos_FRAME, pos_PTU, pos_GPS1, pos_GPS2, pos_GPS3 };

static int inFixed(gpx_t *gpx, int idx, int *frmset, int setcnt) {
    int j;
    for (j = 0; j < N_idx_fixed; j++) {
        if (idx == idx_fixed[j] || idx == idx_fixed[j]+1) return 1;
    }
    if (frametype(gpx) >= -2) {
        if (idx >= pos_ZEROstd && idx < NDATA_LEN) return 1;
    }
    for (j = 0; j < setcnt; j++) {
        if (idx == frmset[j]) return 1;
    }
    return 0;
}

#define rs_N 255
#define rs_R 24
#define rs_K (rs_N-rs_R)

static int rs41_ecc(gpx_t *gpx, int frmlen) {

    int i, j, k, leak, ret = 0;
    int errors1, errors2;
    ui8_t cw1[rs_N], cw2[rs_N];
    ui8_t err_pos1[rs_R], err_pos2[rs_R],
          err_val1[rs_R], err_val2[rs_R];
    ui8_t era_pos[rs_R];
    ui8_t Era_max = 12;

    int frmset[FRAME_LEN];
    int setcnt = 0;


    memset(cw1, 0, rs_N);
    memset(cw2, 0, rs_N);

    if (frmlen > FRAME_LEN) frmlen = FRAME_LEN;
    leak = frmlen % 2;

    for (i = frmlen; i < FRAME_LEN; i++) gpx->frame[i] = 0;


    for (i = 0; i < rs_R; i++) cw1[i] = gpx->frame[cfg_rs41.parpos+i     ];
    for (i = 0; i < rs_R; i++) cw2[i] = gpx->frame[cfg_rs41.parpos+i+rs_R];
    for (i = 0; i < rs_K; i++) cw1[rs_R+i] = gpx->frame[cfg_rs41.msgpos+2*i  ];
    for (i = 0; i < rs_K; i++) cw2[rs_R+i] = gpx->frame[cfg_rs41.msgpos+2*i+1];

    errors1 = rs_decode(&gpx->RS, cw1, err_pos1, err_val1);
    errors2 = rs_decode(&gpx->RS, cw2, err_pos2, err_val2);


    if (gpx->option.ecc >= 2 && (errors1 < 0 || errors2 < 0))
    {
        gpx->frame[pos_FRAME] = (pck_FRAME>>8)&0xFF; gpx->frame[pos_FRAME+1] = pck_FRAME&0xFF;
        gpx->frame[pos_PTU]   = (pck_PTU  >>8)&0xFF; gpx->frame[pos_PTU  +1] = pck_PTU  &0xFF;
        gpx->frame[pos_GPS1]  = (pck_GPS1 >>8)&0xFF; gpx->frame[pos_GPS1 +1] = pck_GPS1 &0xFF;
        gpx->frame[pos_GPS2]  = (pck_GPS2 >>8)&0xFF; gpx->frame[pos_GPS2 +1] = pck_GPS2 &0xFF;
        gpx->frame[pos_GPS3]  = (pck_GPS3 >>8)&0xFF; gpx->frame[pos_GPS3 +1] = pck_GPS3 &0xFF;
        if (frametype(gpx) < -2) {
            for (i = NDATA_LEN + 7; i < FRAME_LEN-2; i++) gpx->frame[i] = 0;
        }
        else {
            for (i = NDATA_LEN; i < FRAME_LEN; i++) gpx->frame[i] = 0;
            gpx->frame[pos_ZEROstd  ] = 0x76;
            gpx->frame[pos_ZEROstd+1] = 0x11;
            for (i = pos_ZEROstd+2; i < NDATA_LEN-2; i++) gpx->frame[i] = 0;
            gpx->frame[NDATA_LEN-2] = 0xEC;
            gpx->frame[NDATA_LEN-1] = 0xC7;
        }
        for (i = 0; i < rs_K; i++) cw1[rs_R+i] = gpx->frame[cfg_rs41.msgpos+2*i  ];
        for (i = 0; i < rs_K; i++) cw2[rs_R+i] = gpx->frame[cfg_rs41.msgpos+2*i+1];
        errors1 = rs_decode(&gpx->RS, cw1, err_pos1, err_val1);
        errors2 = rs_decode(&gpx->RS, cw2, err_pos2, err_val2);
    }

    if (gpx->option.ecc == 4)
    {
        int crc = 0;
        float frnb_ts = gpx->ecdat.ts - gpx->ecdat.last_frnb_ts + 0.5f;
        int   frnb = gpx->ecdat.last_frnb + (unsigned)frnb_ts;
        float calfr_ts = gpx->ecdat.ts - gpx->ecdat.last_calfrm_ts + 0.5f;
        int   calfr = (gpx->ecdat.last_calfrm + (unsigned)calfr_ts) % 51;

        if (errors1 < 0) {
            crc = check_CRC(gpx, pos_FRAME, pck_FRAME);
            if (crc)
            {
                if ( gpx->id[0] && strncmp(gpx->frame+pos_SondeID, gpx->id, 8) != 0 )
                {
                    set_bytes(gpx, pos_SondeID, gpx->id, 8, 1, frmset+setcnt);
                    setcnt += 8/2;
                }

                crc = check_CRC(gpx, pos_FRAME, pck_FRAME);
                if (crc && gpx->calfrchk[calfr])
                {
                    if (gpx->frame[pos_CalData] != calfr) {
                    }
                    else {
                        set_bytes(gpx, pos_CalData+1, gpx->calibytes+calfr*16, 16, 1, frmset+setcnt);
                        setcnt += 16/2;
                    }
                }

                crc = check_CRC(gpx, pos_FRAME, pck_FRAME);
                if (crc && ((frnb>>8)&0xFF) != gpx->frame[pos_FrameNb+1]) {
                    if (gpx->ecdat.last_frnb > 0) {
                        gpx->frame[pos_FrameNb+1] = (frnb>>8)&0xFF;
                        frmset[setcnt++] = pos_FrameNb+1;
                    }
                }
            }

            for (i = 0; i < rs_K; i++) cw1[rs_R+i] = gpx->frame[cfg_rs41.msgpos+2*i  ];
            errors1 = rs_decode(&gpx->RS, cw1, err_pos1, err_val1);
        }
        if (errors2 < 0) {
            crc = check_CRC(gpx, pos_FRAME, pck_FRAME);
            if (crc)
            {
                if ( gpx->id[0] && strncmp(gpx->frame+pos_SondeID, gpx->id, 8) != 0 )
                {
                    set_bytes(gpx, pos_SondeID, gpx->id, 8, 2, frmset+setcnt);
                    setcnt += 8/2;
                }

                crc = check_CRC(gpx, pos_FRAME, pck_FRAME);
                if (crc && gpx->calfrchk[calfr]) {
                    if (gpx->frame[pos_CalData] == calfr) {
                        set_bytes(gpx, pos_CalData+1, gpx->calibytes+calfr*16, 16, 2, frmset+setcnt);
                        setcnt += 16/2;
                    }
                }

                crc = check_CRC(gpx, pos_FRAME, pck_FRAME);
                if (crc && (frnb&0xFF) != gpx->frame[pos_FrameNb]) {
                    if (gpx->ecdat.last_frnb > 0) {
                        gpx->frame[pos_FrameNb] = frnb&0xFF;
                        frmset[setcnt++] = pos_FrameNb;
                    }
                }
            }

            for (i = 0; i < rs_K; i++) cw2[rs_R+i] = gpx->frame[cfg_rs41.msgpos+2*i+1];
            errors2 = rs_decode(&gpx->RS, cw2, err_pos2, err_val2);
        }
    }



    if (gpx->option.ecc > 2)
    {
        int pos_cw = 0;
        int pos_frm = 0;

        if (errors1 < 0)
        {
            for (i = 1; i < Era_max; i++) {
                pos_frm = gpx->ecdat.sort_idx1[i];
                if (inFixed(gpx, pos_frm, frmset, setcnt)) continue;
                if (pos_frm < cfg_rs41.msgpos) pos_cw = pos_frm - cfg_rs41.parpos;
                else                           pos_cw = rs_R + (pos_frm - cfg_rs41.msgpos)/2;
                if (pos_cw < 0 || pos_cw > 254) continue;
                era_pos[0] = pos_cw;
                for (j = 0; j < i; j++) {
                    pos_frm = gpx->ecdat.sort_idx1[j];
                    if (inFixed(gpx, pos_frm, frmset, setcnt)) continue;
                    if (pos_frm < cfg_rs41.msgpos) pos_cw = pos_frm - cfg_rs41.parpos;
                    else                           pos_cw = rs_R + (pos_frm - cfg_rs41.msgpos)/2;
                    if (pos_cw < 0 || pos_cw > 254) continue;
                    era_pos[1] = pos_cw;

                    for (k = -1; k < j; k++)
                    {
                        if (k >= 0) {
                            pos_frm = gpx->ecdat.sort_idx1[k];
                            if (inFixed(gpx, pos_frm, frmset, setcnt)) continue;
                            else {
                                if (pos_frm < cfg_rs41.msgpos) pos_cw = pos_frm - cfg_rs41.parpos;
                                else                           pos_cw = rs_R + (pos_frm - cfg_rs41.msgpos)/2;
                                if (pos_cw < 0 || pos_cw > 254) continue;
                                cw1[pos_cw] ^= gpx->dfrm_bitscore[pos_frm];
                            }
                        }

                        errors1 = rs_decode_ErrEra(&gpx->RS, cw1, 2, era_pos, err_pos1, err_val1);
                        if (errors1 >= 0) { j = 256; i = 256; k = 256; }
                    }
                }
            }
        }

        if (errors2 < 0)
        {
            for (i = 1; i < Era_max; i++) {
                pos_frm = gpx->ecdat.sort_idx2[i];
                if (inFixed(gpx, pos_frm, frmset, setcnt)) continue;
                if (pos_frm < cfg_rs41.msgpos) pos_cw = pos_frm - cfg_rs41.parpos - rs_R;
                else                           pos_cw = rs_R + (pos_frm - cfg_rs41.msgpos)/2;
                if (pos_cw < 0 || pos_cw > 254) continue;
                era_pos[0] = pos_cw;
                for (j = 0; j < i; j++) {
                    pos_frm = gpx->ecdat.sort_idx2[j];
                    if (inFixed(gpx, pos_frm, frmset, setcnt)) continue;
                    if (pos_frm < cfg_rs41.msgpos) pos_cw = pos_frm - cfg_rs41.parpos - rs_R;
                    else                           pos_cw = rs_R + (pos_frm - cfg_rs41.msgpos)/2;
                    if (pos_cw < 0 || pos_cw > 254) continue;
                    era_pos[1] = pos_cw;

                    for (k = -1; k < j; k++)
                    {
                        if (k >= 0) {
                            pos_frm = gpx->ecdat.sort_idx2[k];
                            if (inFixed(gpx, pos_frm, frmset, setcnt)) continue;
                            else {
                                if (pos_frm < cfg_rs41.msgpos) pos_cw = pos_frm - cfg_rs41.parpos - rs_R;
                                else                           pos_cw = rs_R + (pos_frm - cfg_rs41.msgpos)/2;
                                if (pos_cw < 0 || pos_cw > 254) continue;
                                cw2[pos_cw] ^= gpx->dfrm_bitscore[pos_frm];
                            }
                        }

                        errors2 = rs_decode_ErrEra(&gpx->RS, cw2, 2, era_pos, err_pos2, err_val2);
                        if (errors2 >= 0) { j = 256; i = 256; k = 256; }
                    }
                }
            }
        }
    }



    for (i = 0; i < rs_R; i++) {
        gpx->frame[cfg_rs41.parpos+     i] = cw1[i];
        gpx->frame[cfg_rs41.parpos+rs_R+i] = cw2[i];
    }
    for (i = 0; i < rs_K; i++) {
        gpx->frame[cfg_rs41.msgpos+  2*i] = cw1[rs_R+i];
        gpx->frame[cfg_rs41.msgpos+1+2*i] = cw2[rs_R+i];
    }
    if (leak) {
        gpx->frame[cfg_rs41.msgpos+2*i] = cw1[rs_R+i];
    }


    ret = errors1 + errors2;
    if (errors1 < 0 || errors2 < 0) {
        ret = 0;
        if (errors1 < 0) ret |= 0x1;
        if (errors2 < 0) ret |= 0x2;
        ret = -ret;
    }

    return ret;
}


static int prn_frm(gpx_t *gpx) {
    fprintf(stdout, "[%5d] ", gpx->frnr);
    fprintf(stdout, "(%s) ", gpx->id);
    if (gpx->option.vbs == 3) fprintf(stdout, "(%.1f V) ", gpx->batt);
    fprintf(stdout, " ");
    return 0;
}

static int prn_ptu(gpx_t *gpx) {
    fprintf(stdout, " ");
    if (gpx->T > -273.0) fprintf(stdout, " T=%.1fC ", gpx->T);
    if (gpx->RH > -0.5 && gpx->option.ptu != 2)  fprintf(stdout, " _RH=%.0f%% ", gpx->RH);
    if (gpx->P > 0.0) {
        if (gpx->P < 100.0) fprintf(stdout, " P=%.2fhPa ", gpx->P);
        else                fprintf(stdout, " P=%.1fhPa ", gpx->P);
    }
    if (gpx->option.ptu == 2) {
        if (gpx->RH2 > -0.5)  fprintf(stdout, " RH2=%.0f%% ", gpx->RH2);
    }

    if (gpx->option.dwp)
    {
        float rh = gpx->RH;
        float Td = -273.15f;
        if (gpx->option.ptu == 2) rh = gpx->RH2;
        if (rh > 0.0f && gpx->T > -273.0f) {
            float gamma = logf(rh / 100.0f) + (17.625f * gpx->T / (243.04f + gpx->T));
            Td = 243.04f * gamma / (17.625f - gamma);
            fprintf(stdout, " Td=%.1fC ", Td);
        }
    }
    return 0;
}

static int prn_gpstime(gpx_t *gpx) {
    fprintf(stdout, "%s ", weekday[gpx->wday]);
    fprintf(stdout, "%04d-%02d-%02d %02d:%02d:%06.3f",
            gpx->jahr, gpx->monat, gpx->tag, gpx->std, gpx->min, gpx->sek);
    if (gpx->option.vbs == 3) fprintf(stdout, " (W %d)", gpx->week);
    fprintf(stdout, " ");
    return 0;
}

static int prn_gpspos(gpx_t *gpx) {
    fprintf(stdout, " lat: %.5f ", gpx->lat);
    fprintf(stdout, " lon: %.5f ", gpx->lon);
    fprintf(stdout, " alt: %.2f ", gpx->alt);
    fprintf(stdout, "  vH: %4.1f  D: %5.1f  vV: %3.1f ", gpx->vH, gpx->vD, gpx->vV);
    if (gpx->option.vbs == 3) fprintf(stdout, " sats: %02d ", gpx->numSV);
    return 0;
}

static int prn_posdatetime(gpx_t *gpx) {
    fprintf(stdout, "%04d-%02d-%02d %02d:%02d:%05.2f",
            gpx->jahr, gpx->monat, gpx->tag, gpx->std, gpx->min, gpx->sek);
    fprintf(stdout, " ");

    fprintf(stdout, " ");
    fprintf(stdout, " lat: %.5f ", gpx->lat);
    fprintf(stdout, " lon: %.5f ", gpx->lon);
    fprintf(stdout, " alt: %.2f ", gpx->alt);
    fprintf(stdout, "  vH: %4.1f  D: %5.1f  vV: %3.1f ", gpx->vH, gpx->vD, gpx->vV);

    if (gpx->option.vbs == 3) fprintf(stdout, " sats: %02d ", gpx->numSV);

    return 0;
}

static int prn_sat1(gpx_t *gpx, int ofs) {

    fprintf(stdout, "\n");

    fprintf(stdout, "iTOW: 0x%08X", u4(gpx->frame+pos_GPSiTOW+ofs));
    fprintf(stdout, "  week: 0x%04X", u2(gpx->frame+pos_GPSweek+ofs));

    return 0;
}
const double c = 299.792458e6;
const double L1 = 1575.42e6;
static int prn_sat2(gpx_t *gpx, int ofs) {
    int i, n;
    int sv;
    ui32_t minPR;

    fprintf(stdout, "\n");

    minPR = u4(gpx->frame+pos_minPR+ofs);
    fprintf(stdout, "minPR: %d", minPR);
    fprintf(stdout, "\n");

    for (i = 0; i < 12; i++) {
        n = i*7;
        sv = gpx->frame[pos_satsN+ofs+2*i];
        if (sv == 0xFF) break;
        fprintf(stdout, "    SV: %2d ", sv);
        fprintf(stdout, "#  ");
        fprintf(stdout, "prMes: %.1f", u4(gpx->frame+pos_dataSats+ofs+n)/100.0 + minPR);
        fprintf(stdout, "  ");
        fprintf(stdout, "doMes: %.1f", -i3(gpx->frame+pos_dataSats+ofs+n+4)/100.0*L1/c);
        fprintf(stdout, "\n");
    }

    return 0;
}
static int prn_sat3(gpx_t *gpx, int ofs) {
    int numSV;
    double pDOP, sAcc;

    fprintf(stdout, "\n");

    fprintf(stdout, "ECEF-POS: (%d,%d,%d)\n",
                     (i32_t)u4(gpx->frame+pos_GPSecefX+ofs),
                     (i32_t)u4(gpx->frame+pos_GPSecefY+ofs),
                     (i32_t)u4(gpx->frame+pos_GPSecefZ+ofs));
    fprintf(stdout, "ECEF-VEL: (%d,%d,%d)\n",
                     (i16_t)u2(gpx->frame+pos_GPSecefV+ofs+0),
                     (i16_t)u2(gpx->frame+pos_GPSecefV+ofs+2),
                     (i16_t)u2(gpx->frame+pos_GPSecefV+ofs+4));

    numSV = gpx->frame[pos_numSats+ofs];
    sAcc = gpx->frame[pos_sAcc+ofs]/10.0; if (gpx->frame[pos_sAcc+ofs] == 0xFF) sAcc = -1.0;
    pDOP = gpx->frame[pos_pDOP+ofs]/10.0; if (gpx->frame[pos_pDOP+ofs] == 0xFF) pDOP = -1.0;
    fprintf(stdout, "numSatsFix: %2d  sAcc: %.1f  pDOP: %.1f\n", numSV, sAcc, pDOP);

    return 0;
}

static int print_position(gpx_t *gpx, int ec) {
    int i;
    int err = 1;
    int err0 = 1, err1 = 1, err2 = 1, err3 = 1, err13 = 1;
    int encrypted = 0;
    int unexp = 0;
    int out = 1;
    int sat = 0;
    int pos_aux = 0, cnt_aux = 0;
    int ofs_ptu = 0, pck_ptu = 0;
    int isGNSS2 = 0;

    gpx->aux = 0;

    if (gpx->option.sat) sat = 1;
    if (gpx->option.slt) out = 0; else out = 1;

    if ( ec >= 0 )
    {
        int pos, blk, len, crc, pck;
        int flen = NDATA_LEN;

        int ofs_cal = 0;
        int frm_end = NDATA_LEN-2;

        if (frametype(gpx) < 0) flen += XDATA_LEN;

        switch (gpx->frame[pos_PTU]) {
            case 0x7A:
                    frm_end = flen-2;
                    break;
            case 0x7F:
                    frm_end = pos_ZEROstd + 0x1B-0x2A - 2;
                    break;
            case 0x80:
                    frm_end = pos_PTU + 2 + 0xA7;
                    break;
        }

        pos = pos_FRAME;
        gpx->crc = 0;

        while (pos < flen-1) {
            blk = gpx->frame[pos];
            len = gpx->frame[pos+1];
            crc = check_CRC(gpx, pos, blk<<8);
            pck = (blk<<8) | len;

            if ( crc == 0 )
            {
                int ofs = 0;
                switch (pck)
                {
                    case pck_FRAME:
                            ofs = pos - pos_FRAME;
                            ofs_cal = ofs;
                            err = get_FrameConf(gpx, ofs);
                            if ( !err ) {
                                if (out || sat) prn_frm(gpx);
                            }
                            break;

                    case pck_PTU:
                            ofs_ptu = pos - pos_PTU;
                            pck_ptu = pck_PTU;
                            if ( 0 && gpx->option.ptu ) {
                            }
                            break;

                    case pck_GPS1:
                            ofs = pos - pos_GPS1;
                            err1 = get_GPS1(gpx, ofs);
                            if ( !err1 ) {
                                Gps2Date(gpx);
                                if (out) prn_gpstime(gpx);
                                if (sat) prn_sat1(gpx, ofs);
                            }
                            break;

                    case pck_GPS2:
                            ofs = pos - pos_GPS2;
                            err2 = get_GPS2(gpx, ofs);
                            if ( !err2 ) {
                                if (sat) prn_sat2(gpx, ofs);
                            }
                            break;

                    case pck_GPS3:
                            ofs = pos - pos_GPS3;
                            err3 = get_GPS3(gpx, ofs);
                            if ( !err3 ) {
                                if (out) prn_gpspos(gpx);
                                if (sat) prn_sat3(gpx, ofs);
                            }
                            break;

                    case pck_SGM_xTU:
                            ofs_ptu = pos - pos_PTU;
                            pck_ptu = pck;
                            if ( 0 ) {
                            }
                            break;

                    case pck_SGM_CRYPT:
                            encrypted = 1;
                            if (out) fprintf(stdout, " [%04X] (RS41-SGM) ", pck_SGM_CRYPT);
                            break;

                    case pck_960A:
                            break;

                    case pck_8226_POSDATETIME:
                            err13 = get_posdatetime(gpx, pos);
                            if ( !err13 ) {
                                if (out) prn_posdatetime(gpx);
                            }
                            break;

                    case pck_8329_SATS:
                            err2 = get_gnssSVs(gpx, pos);
                            isGNSS2 = 1;
                            break;

                    default:
                            if (blk == 0x7E) {
                                if (pos_aux == 0) pos_aux = pos;
                                cnt_aux += 1;
                            }
                            if (blk == 0x76) {
                            }

                            if (blk != 0x76 && blk != 0x7E) {
                                if (out) fprintf(stdout, " [%04X] ", pck);
                                unexp = 1;
                            }
                }
            }
            else {
                fprintf(stdout, " [ERROR]\n");
                break;
            }

            pos += 2+len+2;

            if ( pos > frm_end )
            {
                if (gpx->option.ptu && !sat && !encrypted && pck_ptu > 0) {
                    err0 = get_PTU(gpx, ofs_ptu, pck_ptu, !err3);
                    if (!err0 && out) prn_ptu(gpx);
                }
                pck_ptu = 0;

                get_Calconf(gpx, out, ofs_cal);

                if (out && ec > 0 && pos > flen-1) fprintf(stdout, " (%d)", ec);

                if (pos_aux) gpx->aux = get_Aux(gpx, out && gpx->option.vbs > 1, pos_aux);

                gpx->crc = 0;
                frm_end = FRAME_LEN-2;

                if ( isGNSS2 ) {
                    if (sat && !err2) prn_gnss_sat2(gpx);
                }

                if (out || sat) fprintf(stdout, "\n");


                if (gpx->option.jsn) {
                    if ( !err && ((!err1 && !err3) || !err13 || encrypted) ) {
                        char *ver_jsn = NULL;
                        fprintf(stdout, "{ \"type\": \"%s\"", "RS41");
                        fprintf(stdout, ", \"frame\": %d, \"id\": \"%s\", \"datetime\": \"%04d-%02d-%02dT%02d:%02d:%06.3fZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f, \"vel_h\": %.5f, \"heading\": %.5f, \"vel_v\": %.5f, \"sats\": %d, \"bt\": %d, \"batt\": %.2f",
                                       gpx->frnr, gpx->id, gpx->jahr, gpx->monat, gpx->tag, gpx->std, gpx->min, gpx->sek, gpx->lat, gpx->lon, gpx->alt, gpx->vH, gpx->vD, gpx->vV, gpx->numSV, gpx->conf_cd, gpx->batt );
                        if (gpx->txpower_valid) {
                            fprintf(stdout, ", \"tx_power_raw\": %d", gpx->txpower );
                        }
                        if (gpx->option.ptu && !err0) {
                            float _RH = gpx->RH;
                            if (gpx->option.ptu == 2) _RH = gpx->RH2;
                            if (gpx->T > -273.0) {
                                fprintf(stdout, ", \"temp\": %.1f",  gpx->T );
                            }
                            if (_RH > -0.5) {
                                fprintf(stdout, ", \"humidity\": %.1f",  _RH );
                            }
                            if (gpx->P > 0.0) {
                                fprintf(stdout, ", \"pressure\": %.2f",  gpx->P );
                            }
                        }
                        if (gpx->aux) {
                            fprintf(stdout, ", \"aux\": \"%s\"",  gpx->xdata );
                        }
                        if (encrypted) {
                            fprintf(stdout, ", \"subtype\": \"RS41-SGM\", \"encrypted\": true");
                        } else {
                            fprintf(stdout, ", \"subtype\": \"%s\"",  *gpx->rstyp ? gpx->rstyp : "RS41" );
                            if (strncmp(gpx->rstyp, "RS41-SGM", 8) == 0) {
                                fprintf(stdout, ", \"encrypted\": false");
                            }
                        }
                        if (gpx->jsn_freq > 0) {
                            int fq_kHz = gpx->jsn_freq;
                            if (gpx->freq > 0) fq_kHz = gpx->freq;
                            fprintf(stdout, ", \"freq\": %d", fq_kHz);
                        }
                        if (*gpx->rsm) {
                            fprintf(stdout, ", \"rs41_mainboard\": \"%s\"", gpx->rsm);
                        }
                        if (gpx->conf_fw) {
                            fprintf(stdout, ", \"rs41_mainboard_fw\": %d", gpx->conf_fw);
                        }

                        if (gpx->option.cal == 1) {
                            int _j;
                            if ( !gpx->calconf_sent && gpx->calconf_complete ) {
                                fprintf(stdout, ", \"rs41_calconf51x16\": \"");
                                for (_j = 0; _j < 51*16; _j++) {
                                    fprintf(stdout, "%02X", gpx->calibytes[_j]);
                                }
                                fprintf(stdout, "\"");
                                gpx->calconf_sent = 1;
                            }
                            if (gpx->calconf_subfrm[0] == 0x32) {
                                fprintf(stdout, ", \"rs41_conf0x32\": \"");
                                for (_j = 0; _j < 16; _j++) {
                                    fprintf(stdout, "%02X", gpx->calconf_subfrm[1+_j]);
                                }
                                fprintf(stdout, "\"");
                            }
                        }
                        if (gpx->option.cal == 2) {
                            int _j;
                            fprintf(stdout, ", \"rs41_subfrm\": \"0x%02X:", gpx->calconf_subfrm[0]);
                            for (_j = 0; _j < 16; _j++) {
                                fprintf(stdout, "%02X", gpx->calconf_subfrm[1+_j]);
                            }
                            fprintf(stdout, "\"");
                        }

                        if (gpx->freq > 0) {
                            fprintf(stdout, ", \"tx_frequency\": %d", gpx->freq );
                        }

                        fprintf(stdout, ", \"ref_datetime\": \"%s\"", gpx->isUTC ? "UTC" : "GPS" );
                        fprintf(stdout, ", \"ref_position\": \"%s\"", "GPS" );

                        #ifdef VER_JSN_STR
                            ver_jsn = VER_JSN_STR;
                        #endif
                        if (ver_jsn && *ver_jsn != '\0') fprintf(stdout, ", \"version\": \"%s\"", ver_jsn);
                        fprintf(stdout, " }\n");
                        fprintf(stdout, "\n");
                    }
                }
            }
        }
    }
    if (ec < 0 && (out || sat  )) {
        int pck, ofs;
        int output = 0, out_mask;

        gpx->crc = 0;
        out_mask = crc_FRAME|crc_GPS1|crc_GPS3;
        if (gpx->option.ptu) out_mask |= crc_PTU;

        err = get_FrameConf(gpx, 0);
        if (out && !err) {
            prn_frm(gpx);
            output = 1;
        }

        pck = (gpx->frame[pos_PTU]<<8) | gpx->frame[pos_PTU+1];
        ofs = 0;

        if (pck < 0x8000) {
            if      (pck == pck_PTU)     ofs = 0;
            else if (pck == pck_SGM_xTU) ofs = 0x1B-0x2A;

            err1 = get_GPS1(gpx, ofs);
            err2 = get_GPS2(gpx, ofs);
            err3 = get_GPS3(gpx, ofs);
            if (!err1) Gps2Date(gpx);

            err0 = get_PTU(gpx, 0, pck, !err3);

            if (out) {

                if (!err1) prn_gpstime(gpx);
                if (!err3) prn_gpspos(gpx);
                if (!err0 && gpx->option.ptu) prn_ptu(gpx);
                if (0 && !err) get_Calconf(gpx, out, 0);

                output = ((gpx->crc & out_mask) != out_mask);

                if (output) {
                    fprintf(stdout, " ");
                    fprintf(stdout, "[");
                    for (i=0; i<5; i++) fprintf(stdout, "%d", (gpx->crc>>i)&1);
                    fprintf(stdout, "]");
                }
            }
        }
        else if (pck == pck_SGM_CRYPT) {
            if (out && !err) {
                fprintf(stdout, " [%04X] (RS41-SGM) ", pck_SGM_CRYPT);
                output = 1;
            }
        }

        if (out && output)
        {
            if      (ec == -1)  fprintf(stdout, " (-+)");
            else if (ec == -2)  fprintf(stdout, " (+-)");
            else     fprintf(stdout, " (--)");

            fprintf(stdout, "\n");
        }
    }


    return  0;
}

static void print_frame(gpx_t *gpx, int len) {
    int i, j, ec = 0, ft;
    int j1 = 0;
    int j2 = 0;
    int sort_score_idx[FRAME_LEN];
    float max_minscore = 0.0;

    gpx->crc = 0;

    if (len < pos_GPS1) {
        for (i = len; i < FRAME_LEN; i++) gpx->frame[i] = 0;
    }

    ft = frametype(gpx);
    if (ft >= 0) len = NDATA_LEN;
    else         len = FRAME_LEN;


    for (i = FRAMESTART; i < len; i++) {
        if (fabs(gpx->ecdat.frm_bytescore[i]) > max_minscore) max_minscore = fabs(gpx->ecdat.frm_bytescore[i]);
    }
    max_minscore = floor(max_minscore+1.5);
    if (gpx->option.ecc > 2) {
        for (i = 0; i < FRAMESTART; i++)  gpx->ecdat.frm_bytescore[i] = max_minscore*2.0;
        for (i = len; i < FRAME_LEN; i++) gpx->ecdat.frm_bytescore[i] = max_minscore;
    }
    for (i = 0; i < FRAME_LEN; i++) sort_score_idx[i] = i;
    if (gpx->option.ecc > 2) {
        for (i = 0; i < FRAME_LEN; i++) {
            for (j = 0; j < FRAME_LEN-1; j++) {
                if (fabs(gpx->ecdat.frm_bytescore[sort_score_idx[j+1]]) < fabs(gpx->ecdat.frm_bytescore[sort_score_idx[j]])) {
                    int tmp = sort_score_idx[j+1];
                    sort_score_idx[j+1] = sort_score_idx[j];
                    sort_score_idx[j] = tmp;
                }
            }
        }
        for (i = 0; i < FRAME_LEN; i++) gpx->ecdat.sort_idx1[i] = i;
        for (i = 0; i < FRAME_LEN; i++) gpx->ecdat.sort_idx2[i] = i;
        j1 = 0;
        j2 = 0;
        for (i = 0; i < FRAME_LEN; i++) {
            if      (sort_score_idx[i] >= cfg_rs41.parpos      && sort_score_idx[i] < cfg_rs41.parpos+  rs_R) gpx->ecdat.sort_idx1[j1++] = sort_score_idx[i];
            else if (sort_score_idx[i] >= cfg_rs41.parpos+rs_R && sort_score_idx[i] < cfg_rs41.parpos+2*rs_R) gpx->ecdat.sort_idx2[j2++] = sort_score_idx[i];
            else if (sort_score_idx[i] >= cfg_rs41.msgpos      && sort_score_idx[i] % 2 == 0)                 gpx->ecdat.sort_idx1[j1++] = sort_score_idx[i];
            else if (sort_score_idx[i] >= cfg_rs41.msgpos      && sort_score_idx[i] % 2 == 1)                 gpx->ecdat.sort_idx2[j2++] = sort_score_idx[i];
        }
    }


    if (gpx->option.ecc) {
        ec = rs41_ecc(gpx, len);
    }


    if (gpx->option.raw) {
        for (i = 0; i < len; i++) {
            fprintf(stdout, "%02x", gpx->frame[i]);
        }
        if (gpx->option.ecc) {
            if (ec >= 0) fprintf(stdout, " [OK]"); else fprintf(stdout, " [NO]");
            if (gpx->option.ecc  ) {
                if (ec > 0) fprintf(stdout, " (%d)", ec);
                if (ec < 0) {
                    if      (ec == -1)  fprintf(stdout, " (-+)");
                    else if (ec == -2)  fprintf(stdout, " (+-)");
                    else     fprintf(stdout, " (--)");
                }
            }
        }
        fprintf(stdout, "\n");
        if (gpx->option.slt  ) {
            print_position(gpx, ec);
        }
    }
    else {
        print_position(gpx, ec);
    }
}



int main(int argc, char *argv[]) {

    int option_min = 0;
    int option_iq = 0;
    int option_iqdc = 0;
    int option_lp = 0;
    int option_dc = 0;
    int option_noLUT = 0;
    int option_bin = 0;
    int option_softin = 0;
    int option_pcmraw = 0;
    int wavloaded = 0;
    int sel_wavch = 0;
    int rawhex = 0, xorhex = 0;
    int cfreq = -1;

    FILE *fp;
    char *fpname = NULL;

    int k;

    char bitbuf[8];
    int bitpos = 0,
        b8pos = 0,
        byte_count = FRAMESTART;
    int bit, byte;
    int bitQ;
    int difbyte = 0;
    hsbit_t hsbit, hsbit1;

    int header_found = 0;

    float thres = 0.7;
    float _mv = 0.0;

    float lpIQ_bw = 7.4e3;

    int symlen = 1;
    int bitofs = 2;
    int shift = 0;

    pcm_t pcm = {0};
    dsp_t dsp = {0};

    gpx_t gpx = {0};

    hdb_t hdb = {0};
    float softbits[BITS];


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
            fprintf(stderr, "       -v, -vx, -vv  (info, aux, info/conf)\n");
            fprintf(stderr, "       -r, --raw\n");
            fprintf(stderr, "       -i, --invert\n");
            fprintf(stderr, "       --ths <x>    (peak threshold; default=%.1f)\n", thres);
            fprintf(stderr, "       --iq0,2,3    (IQ data)\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            gpx.option.vbs = 1;
        }
        else if   (strcmp(*argv, "-vx") == 0) { gpx.option.vbs = 2; }
        else if   (strcmp(*argv, "-vv") == 0) { gpx.option.vbs = 3; }
        else if   (strcmp(*argv, "--aux") == 0) { gpx.option.aux = 1; }
        else if   (strcmp(*argv, "--crc") == 0) { gpx.option.crc = 1; }
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            gpx.option.raw = 1;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            gpx.option.inv = 1;
        }
        else if   (strcmp(*argv, "--ecc" ) == 0) { gpx.option.ecc = 1; }
        else if   (strcmp(*argv, "--ecc2") == 0) { gpx.option.ecc = 2; }
        else if   (strcmp(*argv, "--ecc3") == 0) { gpx.option.ecc = 3; }
        else if   (strcmp(*argv, "--ecc4") == 0) { gpx.option.ecc = 4; }
        else if   (strcmp(*argv, "--sat") == 0) { gpx.option.sat = 1; }
        else if   (strcmp(*argv, "--ptu" ) == 0) { gpx.option.ptu = 1; }
        else if   (strcmp(*argv, "--ptu2") == 0) { gpx.option.ptu = 2; }
        else if   (strcmp(*argv, "--dewp") == 0) { gpx.option.dwp = 1; }
        else if   (strcmp(*argv, "--ch2") == 0) { sel_wavch = 1; }
        else if   (strcmp(*argv, "--auto") == 0) { gpx.option.aut = 1; }
        else if   (strcmp(*argv, "--bin") == 0) { option_bin = 1; }
        else if   (strcmp(*argv, "--softin") == 0)  { option_softin = 1; }
        else if   (strcmp(*argv, "--softinv") == 0) { option_softin = 2; }
        else if   (strcmp(*argv, "--silent") == 0) { gpx.option.slt = 1; }
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
            gpx.option.jsn = 1;
            gpx.option.ecc = 2;
            gpx.option.crc = 1;
        }
        else if   (strcmp(*argv, "--jsn_cfq") == 0) {
            int frq = -1;
            ++argv;
            if (*argv) frq = atoi(*argv); else return -1;
            if (frq < 300000000) frq = -1;
            cfreq = frq;
        }
        else if   (strcmp(*argv, "--jsnsubfrm1") == 0) { gpx.option.cal = 1; }
        else if   (strcmp(*argv, "--jsnsubfrm2") == 0) { gpx.option.cal = 2; }
        else if   (strcmp(*argv, "--rawhex") == 0) { rawhex = 2; }
        else if   (strcmp(*argv, "--xorhex") == 0) { rawhex = 2; xorhex = 1; }
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


    if (gpx.option.raw && gpx.option.jsn) gpx.option.slt = 1;

    if (gpx.option.ecc < 2) gpx.option.ecc = 1;

    if (gpx.option.ecc) {
        rs_init_RS255(&gpx.RS);
    }

    if (gpx.option.aux) gpx.option.vbs = 2;

    memcpy(gpx.frame, rs41_header_bytes, sizeof(rs41_header_bytes));

    gpx.calconf_subfrm = gpx.frame+pos_CalData;
    if (gpx.option.cal) {
        gpx.option.jsn = 1;
        gpx.option.ecc = 2;
        gpx.option.crc = 1;
    }

    if (cfreq > 0) gpx.jsn_freq = (cfreq+500)/1000;


    #ifdef EXT_FSK
    if (!option_bin && !option_softin) {
        option_softin = 1;
        fprintf(stderr, "reading float32 soft symbols\n");
    }
    #endif

    if (!rawhex) {

        if (!option_bin && !option_softin) {

            if (option_iq == 0 && option_pcmraw) {
                fclose(fp);
                fprintf(stderr, "error: raw data not IQ\n");
                return -1;
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
            dsp.hdr = rs41_header;
            dsp.hdrlen = strlen(rs41_header);
            dsp.BT = 0.5;
            dsp.h = 0.6;
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

            bitofs += shift;
        }
        else {
            if (option_bin && option_softin) option_bin = 0;
            hdb.hdr = rs41_header;
            hdb.len = strlen(rs41_header);
            hdb.thb = 1.0 - 3.1/(float)hdb.len;
            hdb.bufpos = -1;
            hdb.buf = calloc(hdb.len, sizeof(char));
            if (hdb.buf == NULL) {
                fprintf(stderr, "error: malloc\n");
                return -1;
            }
            hdb.ths = 0.7;
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
                byte_count = FRAMESTART;
                bitpos = 0;
                b8pos = 0;
                difbyte = 0;

                while ( byte_count < FRAME_LEN )
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
                        if (option_iq > 2) bl = 2.0;
                        bitQ = read_softbit2p(&dsp, &hsbit, 0, bitofs, bitpos, bl, 0, &hsbit1);
                        bit = hsbit.hb;
                        if (gpx.option.ecc >= 3) bit = (hsbit.sb+hsbit1.sb)>=0;

                        if (bitpos < FRAME_LEN*BITS && hsbit.sb*hsbit1.sb < 0) {
                            difbyte |= 1<<b8pos;
                        }
                    }
                    if ( bitQ == EOF ) break;

                    if (gpx.option.inv) {
                        bit ^= 1;
                        hsbit.hb ^= 1;
                        hsbit.sb = -hsbit.sb;
                    }

                    softbits[b8pos] = hsbit.sb;

                    bitpos += 1;
                    bitbuf[b8pos] = bit;
                    b8pos++;
                    if (b8pos == BITS) {
                        int j, j0 = 0;
                        float min_score_byte = softbits[0];
                        for (j = 1; j < BITS; j++) {
                            if (fabs(softbits[j]) < fabs(min_score_byte)) {
                                min_score_byte = softbits[j];
                                j0 = j;
                            }
                        }
                        gpx.ecdat.frm_bytescore[byte_count] = min_score_byte;
                        b8pos = 0;
                        byte = bits2byte(bitbuf);
                        gpx.frame[byte_count] = byte ^ mask[byte_count % MASK_LEN];
                        gpx.dfrm_bitscore[byte_count] = (1<<j0);
                        difbyte = 0;
                        byte_count++;
                    }
                }
                gpx.ecdat.ts = dsp.mv_pos/(float)dsp.sr;

                print_frame(&gpx, byte_count);
                byte_count = FRAMESTART;
                header_found = 0;
            }
        }

        if (!option_bin && !option_softin) free_buffers(&dsp);
        else {
            if (hdb.buf) { free(hdb.buf); hdb.buf = NULL; }
        }
    }
    else
    {
        char buffer_rawhex[2*FRAME_LEN+12];
        char *pbuf = NULL, *buf_sp = NULL;
        ui8_t frmbyte;
        int frameofs = 0, len, i;

        while (1 > 0) {

            pbuf = fgets(buffer_rawhex, 2*FRAME_LEN+12, fp);
            if (pbuf == NULL) break;
            buffer_rawhex[2*FRAME_LEN] = '\0';
            buf_sp = strchr(buffer_rawhex, ' ');
            if (buf_sp != NULL && buf_sp-buffer_rawhex < 2*FRAME_LEN) {
                buffer_rawhex[buf_sp-buffer_rawhex] = '\0';
            }
            len = strlen(buffer_rawhex) / 2;
            if (len > pos_SondeID+10) {
                for (i = 0; i < len; i++) {
                    sscanf(buffer_rawhex+2*i, "%2hhx", &frmbyte);
                    if (xorhex) frmbyte ^= mask[(frameofs+i) % MASK_LEN];
                    gpx.frame[frameofs+i] = frmbyte;
                }
                print_frame(&gpx, frameofs+len);
            }
        }
    }


    fclose(fp);

    return 0;
}