

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


typedef struct {
    i8_t vbs;
    i8_t raw;
    i8_t crc;
    i8_t ecc;
    i8_t sat;
    i8_t ptu;
    i8_t inv;
    i8_t aut;
    i8_t col;
    i8_t jsn;
    i8_t slt;
} option_t;


#define BAUD_M10   9615
#define BAUD_M20   9600



#define BITS 8
#define HEADLEN 32
#define HEADOFS  0
static char rawheader[] = "10011001100110010100110010011001";

#define FRAME_LEN       (100+1)
#define BITFRAME_LEN    (FRAME_LEN*BITS)

#define AUX_LEN          64
#define BITAUX_LEN      (AUX_LEN*BITS)


#define t_M2K2     0x8F
#define t_M10      0x9F
#define t_M10dub   0x49
#define t_M10plus  0xAF
#define t_M20      0x20

typedef struct {
    ui32_t gps_cnt;
    ui8_t cnt;
    ui8_t _diffcnt;
    int week; int tow_ms; int gpssec;
    int jahr; int monat; int tag;
    int wday;
    int std; int min; float sek;
    double lat; double lon; double alt;
    double vH; double vD; double vV;
    double vx; double vy; double vD2;
    float T;  float RH; float TH; float P;
    float Ti; float batV;
    ui8_t numSV;
    ui8_t utc_ofs;
    ui8_t fwVer;
    char SN[12+4];
    ui8_t SNraw[3+2];
    ui8_t frame_bytes[FRAME_LEN+AUX_LEN+4];
    char frame_bits[BITFRAME_LEN+BITAUX_LEN+8];
    int auxlen;
    int jsn_freq;
    option_t option;
    ui8_t type;
    ui8_t frm_ok;
    ui8_t m10bd9616;
} gpx_t;


#define SECONDS_IN_WEEK  (604800.0)
static void Gps2Date(long GpsWeek, long GpsSeconds, int *Year, int *Month, int *Day) {

    long GpsDays, Mjd;
    long J, C, Y, M;

    GpsDays = GpsWeek * 7 + (GpsSeconds / 86400);
    Mjd = 44244 + GpsDays;

    J = Mjd + 2468570;
    C = 4 * J / 146097;
    J = J - (146097 * C + 3) / 4;
    Y = 4000 * (J + 1) / 1461001;
    J = J - 1461 * Y / 4 + 31;
    M = 80 * J / 2447;
    *Day = J - 2447 * M / 80;
    J = M / 11;
    *Month = M + 2 - (12 * J);
    *Year = 100 * (C - 49) + Y + J;
}

static int bits2bytes(char *bitstr, ui8_t *bytes) {
    int i, bit, d, byteval;
    int bitpos, bytepos;

    bitpos = 0;
    bytepos = 0;

    while (bytepos < FRAME_LEN+AUX_LEN) {

        byteval = 0;
        d = 1;
        for (i = 0; i < BITS; i++) {
            bit=*(bitstr+bitpos+7-i);
            if         (bit == '1')                     byteval += d;
            else    byteval += 0;
            d <<= 1;
        }
        bitpos += BITS;
        bytes[bytepos++] = byteval & 0xFF;

    }


    return 0;
}






#define m20stdFLEN       0x45
#define pos_m20GPSTOW    0x0F
#define pos_m20GPSlat    0x1C
#define pos_m20GPSlon    0x20
#define pos_m20GPSalt    0x08
#define pos_m20GPSweek   0x1A
#define pos_m20GPSvE     0x0B
#define pos_m20GPSvN     0x0D
#define pos_m20GPSvU     0x18

#define pos_m20SN        0x12
#define pos_m20CNT       0x15
#define pos_m20BlkChk    0x16
#define pos_m20stdFW     0x43
#define pos_m20stdCheck  (m20stdFLEN-1)

#define len_BlkChk    0x16

#define m10stdFLEN        0x64
#define pos_m10GPSTOW     0x0A
#define pos_m10GPSlat     0x0E
#define pos_m10GPSlon     0x12
#define pos_m10GPSalt     0x16
#define pos_m10GPSsats    0x1E
#define pos_m10GPSutc     0x1F
#define pos_m10GPSweek    0x20
#define pos_m10GPSvE      0x04
#define pos_m10GPSvN      0x06
#define pos_m10GPSvU      0x08

#define pos_m10SN         0x5D
#define pos_m10CNT        0x62
#define pos_m10Check     (m10stdFLEN-1)

#define pos_gtopGPSlat    0x04
#define pos_gtopGPSlon    0x08
#define pos_gtopGPSalt    0x0C
#define pos_gtopGPSvE     0x0F
#define pos_gtopGPSvN     0x11
#define pos_gtopGPSvU     0x13
#define pos_gtopGPStime   0x15
#define pos_gtopGPSdate   0x18


#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define XTERM_COLOR_BROWN   "\x1b[38;5;94m"

#define col_Mtype      "\x1b[38;5;250m"
#define col_GPSweek    "\x1b[38;5;20m"
#define col_GPSTOW     "\x1b[38;5;27m"
#define col_GPSdate    "\x1b[38;5;94m"
#define col_GPSlat     "\x1b[38;5;34m"
#define col_GPSlon     "\x1b[38;5;70m"
#define col_GPSalt     "\x1b[38;5;82m"
#define col_GPSvel     "\x1b[38;5;36m"
#define col_SN         "\x1b[38;5;58m"
#define col_CNT        "\x1b[38;5;172m"
#define col_Check      "\x1b[38;5;11m"
#define col_TXT        "\x1b[38;5;244m"
#define col_FRTXT      "\x1b[38;5;244m"
#define col_CSok       "\x1b[38;5;2m"
#define col_CSoo       "\x1b[38;5;220m"
#define col_CSno       "\x1b[38;5;1m"
#define col_CNST       "\x1b[38;5;58m"
#define col_ptuP       "\x1b[38;5;180m"
#define col_ptuT       "\x1b[38;5;110m"
#define col_ptuU       "\x1b[38;5;120m"
#define col_ptuTH      "\x1b[38;5;115m"


#define COLOPT(tcol)  ((gpx->option.col)?(tcol):(""))


static int get_GPSweek(gpx_t *gpx) {
    int i;
    int pos = 0;
    int gpsweek;

    if (gpx->type == t_M10)
    {
        gpx->numSV   = gpx->frame_bytes[pos_m10GPSsats];
        gpx->utc_ofs = gpx->frame_bytes[pos_m10GPSutc];
    }

    pos = (gpx->type == t_M20) ? pos_m20GPSweek : pos_m10GPSweek;
    gpsweek = (gpx->frame_bytes[pos] << 8) + gpx->frame_bytes[pos+1];
    if (gpsweek > 4000) return -1;

    if (gpsweek < 1304   ) gpsweek += 1024;

    gpx->week = gpsweek;

    return 0;
}

static char weekday[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static int get_GPStime(gpx_t *gpx) {
    int pos = 0;
    int i, ret = 0;
    int gpstime, day;
    int ms = 0;
    double sec_gps0 = 0.0;

    gpstime = 0;
    if (gpx->type == t_M20) {
        for (i = 0; i < 3; i++) {
            gpstime |= gpx->frame_bytes[pos_m20GPSTOW + i] << (8*(2-i));
        }
        gpx->tow_ms = gpstime*1000;
        ms = 0;
    }
    else {
        gpstime = 0;
        for (i = 0; i < 4; i++) {
            gpstime |= gpx->frame_bytes[pos_m10GPSTOW + i] << (8*(3-i));
        }
        gpx->tow_ms = gpstime;
        ms = gpstime % 1000;
        gpstime /= 1000;
    }

    gpx->gpssec = gpstime;

    day = gpstime / (24 * 3600);
    if ((day < 0) || (day > 6)) return -1;

    gpstime %= (24*3600);

    gpx->wday = day;
    gpx->std =  gpstime/3600;
    gpx->min = (gpstime%3600)/60;
    gpx->sek =  gpstime%60 + ms/1000.0;


    ret = get_GPSweek(gpx);
    if (ret) return ret;

    pos = (gpx->type == t_M20) ? pos_m20CNT : pos_m10CNT;
    sec_gps0 = (double)gpx->week*SECONDS_IN_WEEK + gpx->tow_ms/1e3;
    gpx->gps_cnt = (ui32_t)(sec_gps0+0.5);
    gpx->cnt = gpx->frame_bytes[pos];
    gpx->_diffcnt = (ui8_t)(gpx->gps_cnt - gpx->cnt);

    return 0;
}

static double B60B60 = (1<<30)/90.0;
static double GPS_scale_M20 = 1e6;

static int get_GPSpos(gpx_t *gpx) {
    int i;
    int pos = 0;
    int NB = (gpx->type == t_M20) ? 3 : 4;
    int gpsval;
    double GPS_scale = (gpx->type == t_M20) ? GPS_scale_M20 : B60B60;
    double alt_scale = (gpx->type == t_M20) ? 100.0 : 1000.0;

    pos = (gpx->type == t_M20) ? pos_m20GPSlat : pos_m10GPSlat;
    gpsval = 0;
    for (i = 0; i < 4; i++) {
        gpsval |= gpx->frame_bytes[pos+i] << (8*(3-i));
    }
    gpx->lat = gpsval / GPS_scale;

    pos = (gpx->type == t_M20) ? pos_m20GPSlon : pos_m10GPSlon;
    gpsval = 0;
    for (i = 0; i < 4; i++) {
        gpsval |= gpx->frame_bytes[pos+i] << (8*(3-i));
    }
    gpx->lon = gpsval / GPS_scale;

    pos = (gpx->type == t_M20) ? pos_m20GPSalt : pos_m10GPSalt;
    gpsval = 0;
    for (i = 0; i < NB; i++) {
        gpsval |= gpx->frame_bytes[pos+i] << (8*(NB-1-i));
    }
    gpx->alt = gpsval / alt_scale;

    return 0;
}

static int get_GPSvel(gpx_t *gpx) {
    int i;
    int pos = 0;
    short vel16;
    double vx, vy, dir, alpha;
    double vel_scale = (gpx->type == t_M20) ? 100.0 : 200.0;

    pos = (gpx->type == t_M20) ? pos_m20GPSvE : pos_m10GPSvE;
    vel16 = gpx->frame_bytes[pos] << 8 | gpx->frame_bytes[pos+1];
    vx = vel16 / vel_scale;

    pos = (gpx->type == t_M20) ? pos_m20GPSvN : pos_m10GPSvN;
    vel16 = gpx->frame_bytes[pos] << 8 | gpx->frame_bytes[pos+1];
    vy= vel16 / vel_scale;

    gpx->vx = vx;
    gpx->vy = vy;
    gpx->vH = sqrt(vx*vx+vy*vy);
    alpha = atan2(vy, vx)*180/M_PI;
    dir = 90-alpha;
    if (dir < 0) dir += 360;
    gpx->vD2 = dir;
    dir = atan2(vx, vy) * 180 / M_PI;
    if (dir < 0) dir += 360;
    gpx->vD = dir;

    pos = (gpx->type == t_M20) ? pos_m20GPSvU : pos_m10GPSvU;
    vel16 = gpx->frame_bytes[pos] << 8 | gpx->frame_bytes[pos+1];
    gpx->vV = vel16 / vel_scale;

    return 0;
}


static int get_gtopGPSpos(gpx_t *gpx) {
    int i;
    int val;

    val = 0;
    for (i = 0; i < 4; i++)  val |= gpx->frame_bytes[pos_gtopGPSlat + i] << (8*(3-i));
    gpx->lat = val/1e6;

    val = 0;
    for (i = 0; i < 4; i++)  val |= gpx->frame_bytes[pos_gtopGPSlon + i] << (8*(3-i));
    gpx->lon = val/1e6;

    val = 0;
    for (i = 0; i < 3; i++)  val |= gpx->frame_bytes[pos_gtopGPSalt + i] << (8*(2-i));
    if (val & 0x800000) val -= 0x1000000;
    gpx->alt = val/1e2;

    return 0;
}

static int get_gtopGPSvel(gpx_t *gpx) {
    short vel16;
    double vx, vy, vz, dir;

    vel16 = gpx->frame_bytes[pos_gtopGPSvE] << 8 | gpx->frame_bytes[pos_gtopGPSvE + 1];
    vx = vel16 / 1e2;

    vel16 = gpx->frame_bytes[pos_gtopGPSvN] << 8 | gpx->frame_bytes[pos_gtopGPSvN + 1];
    vy= vel16 / 1e2;

    vel16 = gpx->frame_bytes[pos_gtopGPSvU] << 8 | gpx->frame_bytes[pos_gtopGPSvU + 1];
    vz = vel16 / 1e2;

    gpx->vx = vx;
    gpx->vy = vy;
    gpx->vH = sqrt(vx*vx+vy*vy);
    dir = atan2(vx, vy) * 180 / M_PI;
    if (dir < 0) dir += 360;
    gpx->vD = dir;
    gpx->vV = vz;

    return 0;
}

static int get_gtopGPStime(gpx_t *gpx) {
    int i;
    int time;

    time = 0;
    for (i = 0; i < 3; i++)  time |= gpx->frame_bytes[pos_gtopGPStime + i] << (8*(2-i));

    gpx->std =  time/10000;
    gpx->min = (time%10000)/100;
    gpx->sek = (time%100)/1.0;

    return 0;
}

static int get_gtopGPSdate(gpx_t *gpx) {
    int i;
    int date;

    date = 0;
    for (i = 0; i < 3; i++)  date |= gpx->frame_bytes[pos_gtopGPSdate + i] << (8*(2-i));

    gpx->jahr  = 2000 + date%100;
    gpx->monat = (date%10000)/100;
    gpx->tag   =  date/10000;

    return 0;
}


static int get_m10SN(gpx_t *gpx) {
    int i;
    unsigned byte;

    for (i = 0; i < 11; i++) gpx->SN[i] = ' '; gpx->SN[11] = '\0';

    for (i = 0; i < 5; i++) {
        gpx->SNraw[i] = gpx->frame_bytes[pos_m10SN + i];
    }

    byte = gpx->SNraw[2];
    sprintf(gpx->SN, "%1X%02u", (byte>>4)&0xF, byte&0xF);
    byte = gpx->SNraw[3] | (gpx->SNraw[4]<<8);
    sprintf(gpx->SN+3, "-%1X-%1u%04u", gpx->SNraw[0]&0xF, (byte>>13)&0x7, byte&0x1FFF);

    return 0;
}

static int get_m20SN(gpx_t *gpx) {
    int i;
    ui32_t sn24 = (gpx->frame_bytes[pos_m20SN+2]<<16) | (gpx->frame_bytes[pos_m20SN+1]<<8) | gpx->frame_bytes[pos_m20SN];
    ui8_t ym = sn24 & 0x7F;
    ui8_t y = ym / 12;
    ui8_t m = (ym % 12)+1;

    for (i =  0; i < 11; i++) gpx->SN[i] = ' ';  gpx->SN[11] = '\0';
    for (i = 12; i < 15; i++) gpx->SN[i] = '\0'; gpx->SN[15] = '\0';

    for (i = 0; i < 3; i++) {
        gpx->SNraw[i] = gpx->frame_bytes[pos_m20SN + i];
    }

    sprintf(gpx->SN, "%u%02u", y, m);
    sprintf(gpx->SN+3, "-%u-", ((sn24>> 7)&0x7)+1);
    sprintf(gpx->SN+6, "%u",    (sn24>>23)&0x1);
    sprintf(gpx->SN+7, "%04u",  (sn24>>10)&0x1FFF);


    if (sn24 == 0)
    {
        sprintf(gpx->SN, "%s", "000-0-00000");
        sprintf(gpx->SN+11, "-%03u", gpx->_diffcnt & 0xFF);
    }

    return 0;
}


static int update_checkM10(int c, ui8_t b) {
    int c0, c1, t, t6, t7, s;

    c1 = c & 0xFF;

    b  = (b >> 1) | ((b & 1) << 7);
    b ^= (b >> 2) & 0xFF;

    t6 = ( c     & 1) ^ ((c>>2) & 1) ^ ((c>>4) & 1);
    t7 = ((c>>1) & 1) ^ ((c>>3) & 1) ^ ((c>>5) & 1);
    t = (c & 0x3F) | (t6 << 6) | (t7 << 7);

    s  = (c >> 7) & 0xFF;
    s ^= (s >> 2) & 0xFF;


    c0 = b ^ t ^ s;

    return ((c1<<8) | c0) & 0xFFFF;
}

static int checkM10(ui8_t *msg, int len) {
    int i, cs;

    cs = 0;
    for (i = 0; i < len; i++) {
        cs = update_checkM10(cs, msg[i]);
    }

    return cs & 0xFFFF;
}
static int blk_checkM10(int len, ui8_t *msg) {
    int i, cs;
    ui8_t pre = len & 0xFF;
    cs = 0;

    cs = update_checkM10(cs, pre);

    for (i = 0; i < len-2; i++) {
        cs = update_checkM10(cs, msg[i]);
    }

    return cs & 0xFFFF;
}


static float get_Temp(gpx_t *gpx) {
    float p0 = 1.07303516e-03,
          p1 = 2.41296733e-04,
          p2 = 2.26744154e-06,
          p3 = 6.52855181e-08;

    float Rs[3] = { 12.1e3 ,  36.5e3 ,  475.0e3 };
    float Rp[3] = { 1e20   , 330.0e3 , 2000.0e3 };

    ui8_t  scT = 0;
    ui16_t ADC_RT;

    float x, R;
    float T = 0;

    if (gpx->type == t_M20)
    {
        ADC_RT  = (gpx->frame_bytes[0x5] << 8) | gpx->frame_bytes[0x4];

        if      (ADC_RT > 8191) { scT = 2; ADC_RT -= 8192; }
        else if (ADC_RT > 4095) { scT = 1; ADC_RT -= 4096; }
        else                    { scT = 0; }
    }
    else
    {
        scT     =  gpx->frame_bytes[0x3E];
        ADC_RT  = (gpx->frame_bytes[0x40] << 8) | gpx->frame_bytes[0x3F];
        ADC_RT -= 0xA000;
    }

    R = -1;
    x = (4095.0-ADC_RT)/ADC_RT;
    if (scT < 3) {
        R =  Rs[scT] /( x - Rs[scT]/Rp[scT] );
    }

    if (R > 0)  T = 1.0/( p0 + p1*log(R) + p2*log(R)*log(R) + p3*log(R)*log(R)*log(R) );

    if (T-273.15 < -120.0 || T-273.15 > 60.0) T = 0;

    return  T - 273.15;
}

static float get_Tntc2(gpx_t *gpx) {
    float Rs = 22.1e3;
    float R25 = 2.2e3;
    float b = 3650.0;
    float T25 = 25.0 + 273.15;
    float p0 =  4.42606809e-03,
          p1 = -6.58184309e-04,
          p2 =  8.95735557e-05,
          p3 = -2.84347503e-06;
    float T = 0.0;
    ui16_t ADC_ntc;
    float x, R;

    if (gpx->type == t_M20)
    {
        ADC_ntc  = (gpx->frame_bytes[0x07] << 8) | gpx->frame_bytes[0x06];
        x = (4095.0 - ADC_ntc)/ADC_ntc;
        R = Rs / x;
        if (R > 0)  T = 1.0/(1.0/T25 + 1.0/b * log(R/R25));
    }
    else
    {
        ADC_ntc  = (gpx->frame_bytes[0x5A] << 8) | gpx->frame_bytes[0x59];
        x = (4095.0 - ADC_ntc)/ADC_ntc;
        R = Rs / x;
        if (R > 0)  T =  1/( p0 + p1*log(R) + p2*log(R)*log(R) + p3*log(R)*log(R)*log(R) );
    }

    return T - 273.15;
}

static float get_intTemp_m10(gpx_t *gpx) {
    ui16_t ADC_Ti_raw = (gpx->frame_bytes[0x49] << 8) | gpx->frame_bytes[0x48];
    float vti, ti;
    vti = ADC_Ti_raw/4095.0f * 1.5f;
    ti = (vti-0.986f)/0.00355f;
    return ti;
}
#define FREQ_CAPCLK (8e6/2)
#define LN2         0.693147181
#define ADR_108A    1000.0
static float get_count_55(gpx_t *gpx) {
    ui32_t TBCREF_1000 = gpx->frame_bytes[0x32] | (gpx->frame_bytes[0x33]<<8) | (gpx->frame_bytes[0x34]<<16);
    return TBCREF_1000 / ADR_108A;
}
static float get_count_RH(gpx_t *gpx) {
    ui32_t TBCCR1_1000 = gpx->frame_bytes[0x35] | (gpx->frame_bytes[0x36]<<8) | (gpx->frame_bytes[0x37]<<16);
    return TBCCR1_1000 / ADR_108A;
}
static float get_TLC555freq(gpx_t *gpx) {
    return FREQ_CAPCLK / get_count_RH(gpx);
}
static float cRHc55_RH(gpx_t *gpx, float cRHc55) {
    float TH = get_Tntc2(gpx);
    float Tc = get_Temp(gpx);
    float rh = (cRHc55-0.8955)/0.002;
    float T0 = 0.0, T1 = -30.0;
    float T = Tc;
    if (T < T0) rh += T0 - T/5.5;
    if (T < T1) rh *= 1.0 + (T1-T)/75.0;
    if (rh < 0.0) rh = 0.0;
    if (rh > 100.0) rh = 100.0;
    return rh;
}
static float get_55RH(gpx_t *gpx) {
    float  cRHc55 = get_count_RH(gpx) / get_count_55(gpx);
    return cRHc55_RH(gpx, cRHc55);
}

static float get_m20RHraw(gpx_t *gpx) {
    float _rh = -1.0;
    float _RH = -1.0;
    ui16_t ADC_rh;

    ADC_rh = (gpx->frame_bytes[0x03] << 8) | gpx->frame_bytes[0x02];
    _rh = ADC_rh / (float)(1<<15);

    _RH = -1.0;
    if (_rh < 1.05) _RH = _rh*100.0;


    return _RH;
}

static float get_m20RH(gpx_t *gpx) {
    float TU = get_Tntc2(gpx);
    float RH = -1.0f;
    float x;

    ui16_t humval = (gpx->frame_bytes[0x03] << 8) | gpx->frame_bytes[0x02];
    ui16_t rh_cal = (gpx->frame_bytes[0x30] << 8) | gpx->frame_bytes[0x2F];

    float humidityCalibration = 6.4e8f / (rh_cal + 80000.0f);

    x = (humval + 80000.0f) * humidityCalibration * (1.0f - 5.8e-4f * (TU-25.0f));
    x = 4.16e9f / x;
    x = 10.087f*x*x*x - 211.62f*x*x + 1388.2f*x - 2797.0f;

    RH = -1.0f;
    if (humval < 48000)
    {
        if (x > -20.0f && x < 120.f) {
            RH = x;
            if (RH < 0.0f  ) RH = 0.0f;
            if (RH > 100.0f) RH = 100.0f;
        }
    }


    return RH;
}

static float get_P(gpx_t *gpx) {
    float hPa = 0.0f;

    if (gpx->type == t_M20)
    {
        ui32_t val = (gpx->frame_bytes[0x25] << 8) | gpx->frame_bytes[0x24];
        ui8_t p0 = 0x00;

        if (gpx->fwVer >= 0x07) {
            p0 = gpx->frame_bytes[0x16];
        }
        val = (val << 8) | p0;

        if (val > 0) {
            hPa = val/(float)(16*256);
        }
        if (hPa > 2560.0f) {
            hPa = -1.0f;
        }
    }

    return hPa;
}

static float get_BatV(gpx_t *gpx) {
    float batV = 0.0f;

    if (gpx->type == t_M20)
    {
        ui8_t val = gpx->frame_bytes[0x26];
        batV = val * (3.3f/255);
    }
    else
    {
        ui32_t batADC = 0;
        batADC = (gpx->frame_bytes[0x46] << 8) | gpx->frame_bytes[0x45];

        batV = 2.709 * batADC*2.5/1023.0;
    }

    return batV;
}



static int print_pos(gpx_t *gpx, int len, int bcOK, int csOK) {
    int err, err2;
    int pos_CNT = (gpx->type == t_M20) ? pos_m20CNT : pos_m10CNT;
    int pos_SN = (gpx->type == t_M20) ? pos_m20SN : pos_m10SN;

    err = 0; err2 = 0;
    if (gpx->type != t_M10plus && len > 0x24)
    {
        err |= get_GPStime(gpx);
        err |= get_GPSpos(gpx);
        err2 = get_GPSvel(gpx);
        Gps2Date(gpx->week, gpx->gpssec, &gpx->jahr, &gpx->monat, &gpx->tag);
    }
    else if (gpx->type == t_M10plus && len > 0x24) {
        err |= get_gtopGPStime(gpx);
        err |= get_gtopGPSdate(gpx);
        err |= get_gtopGPSpos(gpx);
        err2 = get_gtopGPSvel(gpx);
    }
    else err = 0xFF;

    if (!err) {

        (gpx->type == t_M20) ? get_m20SN(gpx) : get_m10SN(gpx);

        if (gpx->option.ptu && csOK) {
            gpx->T  = get_Temp(gpx);
            gpx->TH = get_Tntc2(gpx);
            gpx->RH = (gpx->type == t_M20) ? get_m20RH(gpx) : get_55RH(gpx);
            gpx->P  = get_P(gpx);
        }

        gpx->batV = get_BatV(gpx);

        if ( !gpx->option.slt )
        {
            fprintf(stdout, "%s", COLOPT(col_TXT));

            if (gpx->option.vbs >= 3) {
                fprintf(stdout, "[%3d] ", gpx->frame_bytes[pos_CNT]);
            }
            if (gpx->type != t_M10plus) {
                if (gpx->option.vbs >= 3) {
                    fprintf(stdout, "(W %s%d%s) ", COLOPT(col_GPSweek), gpx->week, COLOPT(col_TXT));
                }
                fprintf(stdout, "%s%s%s ", COLOPT(col_GPSTOW), weekday[gpx->wday], COLOPT(col_TXT));
            }
            fprintf(stdout, "%s%04d-%02d-%02d%s %s%02d:%02d:%06.3f%s ",
                    COLOPT(col_GPSdate), gpx->jahr, gpx->monat, gpx->tag, COLOPT(col_TXT),
                    COLOPT(col_GPSTOW), gpx->std, gpx->min, gpx->sek, COLOPT(col_TXT));
            fprintf(stdout, " lat: %s%.5f%s ", COLOPT(col_GPSlat), gpx->lat, COLOPT(col_TXT));
            fprintf(stdout, " lon: %s%.5f%s ", COLOPT(col_GPSlon), gpx->lon, COLOPT(col_TXT));
            fprintf(stdout, " alt: %s%.2f%s ", COLOPT(col_GPSalt), gpx->alt, COLOPT(col_TXT));
            if (!err2) {
                fprintf(stdout, "  vH: %s%4.1f%s  D: %s%5.1f%s  vV: %s%3.1f%s ",
                        COLOPT(col_GPSvel), gpx->vH, COLOPT(col_TXT),
                        COLOPT(col_GPSvel), gpx->vD, COLOPT(col_TXT),
                        COLOPT(col_GPSvel), gpx->vV, COLOPT(col_TXT));
            }
            if (gpx->option.vbs >= 1 && (bcOK || csOK)) {
                fprintf(stdout, "  SN");
                if (gpx->option.vbs >= 3) fprintf(stdout, "[%02X]", gpx->type);
                fprintf(stdout, ": ");
                if (gpx->option.vbs >= 2) fprintf(stdout, "%s-", (gpx->type==t_M20) ? "M20" : "M10");
                fprintf(stdout, "%s%s%s", COLOPT(col_SN), gpx->SN, COLOPT(col_TXT));
            }
            if (gpx->option.vbs >= 1) {
                fprintf(stdout, "  # ");
                if (gpx->type == t_M20 && gpx->fwVer < 0x07) {
                    if      (bcOK > 0) fprintf(stdout, " %s(ok)%s", COLOPT(col_CSok), COLOPT(col_TXT));
                    else if (bcOK < 0) fprintf(stdout, " %s(oo)%s", COLOPT(col_CSoo), COLOPT(col_TXT));
                    else               fprintf(stdout, " %s(no)%s", COLOPT(col_CSno), COLOPT(col_TXT));
                }
                if (csOK) fprintf(stdout, " %s[OK]%s", COLOPT(col_CSok), COLOPT(col_TXT));
                else      fprintf(stdout, " %s[NO]%s", COLOPT(col_CSno), COLOPT(col_TXT));
            }
            if (gpx->option.ptu && csOK) {
                fprintf(stdout, " ");
                if (gpx->T > -273.0f)  fprintf(stdout, " T:%.1fC", gpx->T);
                if (gpx->RH > -0.5f)   fprintf(stdout, " RH=%.0f%%", gpx->RH);
                if (gpx->option.vbs >= 2) {
                    if (gpx->TH > -273.0f) fprintf(stdout, " TH:%.1fC", gpx->TH);
                }
                if (gpx->P > 0.0f) {
                    if      (gpx->P <  10.0f) fprintf(stdout, " P=%.3fhPa ", gpx->P);
                    else if (gpx->P < 100.0f) fprintf(stdout, " P=%.2fhPa ", gpx->P);
                    else                      fprintf(stdout, " P=%.1fhPa ", gpx->P);
                }
                if (gpx->type != t_M20 && gpx->option.vbs >= 3) {
                    float fq555 = get_TLC555freq(gpx);
                    gpx->Ti = get_intTemp_m10(gpx);
                    fprintf(stdout, "  (Ti:%.1fC)", gpx->Ti);
                    fprintf(stdout, " (%.3fkHz)", fq555/1e3);
                }
            }
            if (gpx->option.vbs >= 3 && csOK) {
                fprintf(stdout, " (bat:%.2fV)", gpx->batV);
            }
            fprintf(stdout, "%s", COLOPT(ANSI_COLOR_RESET));

            fprintf(stdout, "\n");
        }


        if (gpx->option.jsn) {
            if (csOK)
            {
                char *ver_jsn = NULL;

                if (gpx->type == t_M20)
                {
                    char sn_id[4+12+4] = "M20-";

                    strncpy(sn_id+4, gpx->SN, 12+4);
                    sn_id[15+4] = '\0';

                    fprintf(stdout, "{ \"type\": \"%s\"", "M20");
                    fprintf(stdout, ", \"frame\": %lu, ", (unsigned long)gpx->gps_cnt);
                    fprintf(stdout, "\"id\": \"%s\", \"datetime\": \"%04d-%02d-%02dT%02d:%02d:%06.3fZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f, \"vel_h\": %.5f, \"heading\": %.5f, \"vel_v\": %.5f",
                                sn_id, gpx->jahr, gpx->monat, gpx->tag, gpx->std, gpx->min, gpx->sek, gpx->lat, gpx->lon, gpx->alt, gpx->vH, gpx->vD, gpx->vV);
                    if (gpx->option.ptu) {
                        if (gpx->T > -273.0f) fprintf(stdout, ", \"temp\": %.1f", gpx->T );
                        if (gpx->RH > -0.5f)  fprintf(stdout, ", \"humidity\": %.1f", gpx->RH );
                        if (gpx->P > 0.0f)    fprintf(stdout, ", \"pressure\": %.2f",  gpx->P );
                    }
                    fprintf(stdout, ", \"batt\": %.2f", gpx->batV);
                    fprintf(stdout, ", \"rawid\": \"M20_%02X%02X%02X\"", gpx->frame_bytes[pos_SN], gpx->frame_bytes[pos_SN+1], gpx->frame_bytes[pos_SN+2]);
                    fprintf(stdout, ", \"subtype\": \"0x%02X\"", gpx->type);
                    if (gpx->jsn_freq > 0) {
                        fprintf(stdout, ", \"freq\": %d", gpx->jsn_freq);
                    }

                    fprintf(stdout, ", \"ref_datetime\": \"%s\"", "GPS" );
                    fprintf(stdout, ", \"ref_position\": \"%s\"", "GPS" );
                }
                else
                {
                    int j;
                    char sn_id[4+12] = "M10-";
                    ui8_t aprs_id[4];
                    double sec_gps0 = (double)gpx->week*SECONDS_IN_WEEK + gpx->tow_ms/1e3;
                    int utc_s = gpx->gpssec - gpx->utc_ofs;
                    int utc_week = gpx->week;
                    int utc_jahr; int utc_monat; int utc_tag;
                    int utc_std; int utc_min; float utc_sek;
                    if (utc_s < 0) {
                        utc_week -= 1;
                        utc_s += 604800;
                    }
                    if (gpx->type == t_M10) {
                        Gps2Date(utc_week, utc_s, &utc_jahr, &utc_monat, &utc_tag);
                        utc_s  %= (24*3600);
                        utc_std =  utc_s/3600;
                        utc_min = (utc_s%3600)/60;
                        utc_sek =  utc_s%60 + (gpx->tow_ms % 1000)/1000.0;
                    }
                    else {
                        utc_jahr  = gpx->jahr;
                        utc_monat = gpx->monat;
                        utc_tag   = gpx->tag;
                        utc_std = gpx->std;
                        utc_min = gpx->min;
                        utc_sek = gpx->sek;
                    }

                    strncpy(sn_id+4, gpx->SN, 12);
                    sn_id[15] = '\0';
                    for (j = 0; sn_id[j]; j++) { if (sn_id[j] == ' ') sn_id[j] = '-'; }

                    fprintf(stdout, "{ \"type\": \"%s\"", "M10");
                    fprintf(stdout, ", \"frame\": %lu, ", (unsigned long)(sec_gps0+0.5));
                    fprintf(stdout, "\"id\": \"%s\", \"datetime\": \"%04d-%02d-%02dT%02d:%02d:%06.3fZ\", \"lat\": %.5f, \"lon\": %.5f, \"alt\": %.5f, \"vel_h\": %.5f, \"heading\": %.5f, \"vel_v\": %.5f",
                                sn_id, utc_jahr, utc_monat, utc_tag, utc_std, utc_min, utc_sek, gpx->lat, gpx->lon, gpx->alt, gpx->vH, gpx->vD, gpx->vV);
                    if (gpx->type == t_M10) {
                        fprintf(stdout, ", \"sats\": %d", gpx->numSV);
                    }
                    aprs_id[0] = gpx->frame_bytes[pos_SN+2];
                    aprs_id[1] = gpx->frame_bytes[pos_SN] & 0xF;
                    aprs_id[2] = gpx->frame_bytes[pos_SN+4];
                    aprs_id[3] = gpx->frame_bytes[pos_SN+3];
                    fprintf(stdout, ", \"aprsid\": \"ME%02X%1X%02X%02X\"", aprs_id[0], aprs_id[1], aprs_id[2], aprs_id[3]);
                    fprintf(stdout, ", \"batt\": %.2f", gpx->batV);
                    if (gpx->option.ptu) {
                        if (gpx->T > -273.0) fprintf(stdout, ", \"temp\": %.1f", gpx->T);
                        if (gpx->option.vbs >= 2) {
                            if (gpx->RH > -0.5) fprintf(stdout, ", \"humidity\": %.1f", gpx->RH);
                        }
                    }
                    fprintf(stdout, ", \"rawid\": \"M10_%02X%02X%02X%02X%02X\"", gpx->frame_bytes[pos_SN], gpx->frame_bytes[pos_SN+1],
                                                gpx->frame_bytes[pos_SN+2], gpx->frame_bytes[pos_SN+3], gpx->frame_bytes[pos_SN+4]);
                    fprintf(stdout, ", \"subtype\": \"0x%02X\"", gpx->type);
                    if (gpx->jsn_freq > 0) {
                        fprintf(stdout, ", \"freq\": %d", gpx->jsn_freq);
                    }

                    fprintf(stdout, ", \"ref_datetime\": \"%s\"", "UTC" );
                    fprintf(stdout, ", \"ref_position\": \"%s\"", "GPS" );
                    fprintf(stdout, ", \"gpsutc_leapsec\": %d", gpx->utc_ofs);
                }
                #ifdef VER_JSN_STR
                    ver_jsn = VER_JSN_STR;
                #endif
                if (ver_jsn && *ver_jsn != '\0') fprintf(stdout, ", \"version\": \"%s\"", ver_jsn);
                fprintf(stdout, " }\n");
                fprintf(stdout, "\n");
            }
        }

    }

    return err;
}

static int print_frame(gpx_t *gpx, int pos, int b2B) {
    int ret = 0;
    int i;
    ui8_t byte;
    int cs1=0, cs2=0;
    int bc1=0, bc2=0, bc=0;
    int stdFLEN = m20stdFLEN;
    int flen = m20stdFLEN;
    int pos_fw = pos_m20stdFW;
    int pos_check = pos_m20stdCheck;

    if (b2B) {
        bits2bytes(gpx->frame_bits, gpx->frame_bytes);
    }

    switch (gpx->frame_bytes[1]) {
        case 0x8F: gpx->type = t_M2K2;    break;
        case 0x9F: gpx->type = t_M10;     break;
        case 0x49: gpx->type = t_M10dub;  break;
        case 0xAF: gpx->type = t_M10plus; break;
        case 0x20: gpx->type = t_M20;     break;
        default  : gpx->type = t_M10;
    }

    if (gpx->type != t_M20) {
        stdFLEN = m10stdFLEN;
        flen = m10stdFLEN;
        pos_check = pos_m10Check;
    }

    flen = gpx->frame_bytes[0];
    if (flen == 0x00) {
        return 0;
    }
    if (pos < flen*8) flen = pos/8;

    if (flen == stdFLEN) gpx->auxlen = 0;
    else {
        gpx->auxlen = flen - stdFLEN;
        if (gpx->auxlen < 0) {
            gpx->auxlen = 0;
            pos_fw = flen-2;
        }
        else if (gpx->auxlen > AUX_LEN) {
            gpx->auxlen = AUX_LEN;
            flen = stdFLEN+AUX_LEN;
        }
    }
    pos_check = flen-1;

    cs1 = (gpx->frame_bytes[pos_check] << 8) | gpx->frame_bytes[pos_check+1];
    cs2 = checkM10(gpx->frame_bytes, pos_check);
    gpx->frm_ok = (cs1 == cs2);

    if (cs1 != cs2) {
        int j, h = 0;
        int _t20=0;
        for (j = 0; j < 4; j++) { h += (gpx->type>>j) & 1; }
        if (h < 2 || h == 2 && (gpx->type & 0xF0) == 0x20) { _t20 = 1; }
        if (_t20) {
            if (gpx->m10bd9616 == 0) { ret = BAUD_M20; }
        }
        else {
            ret = gpx->m10bd9616 ? BAUD_M10
                                 : BAUD_M20;
        }
    }

    if (gpx->type == t_M20) {
        gpx->fwVer = gpx->frame_bytes[pos_fw];
        if (gpx->fwVer > 0x20) gpx->fwVer = 0;

        bc1 = (gpx->frame_bytes[pos_m20BlkChk] << 8) | gpx->frame_bytes[pos_m20BlkChk+1];
        bc2 = blk_checkM10(len_BlkChk, gpx->frame_bytes+2);
        if (bc1 == bc2)    bc = 1;
        else if (bc1 == 0) bc = -1;
        else               bc = 0;
    }

    if (gpx->option.raw) {

        if (1  ) {
            fprintf(stdout, "%s", COLOPT(col_FRTXT));
            for (i = 0; i < flen+1; i++)
            {
                byte = gpx->frame_bytes[i];
                if  (i == 1) fprintf(stdout, "%s", COLOPT(col_Mtype));

                if (gpx->type == t_M20)
                {
                    if ((i >= pos_m20GPSTOW)   &&  (i < pos_m20GPSTOW+3))   fprintf(stdout, "%s", COLOPT(col_GPSTOW));
                    if ((i >= pos_m20GPSlat)   &&  (i < pos_m20GPSlat+4))   fprintf(stdout, "%s", COLOPT(col_GPSlat));
                    if ((i >= pos_m20GPSlon)   &&  (i < pos_m20GPSlon+4))   fprintf(stdout, "%s", COLOPT(col_GPSlon));
                    if ((i >= pos_m20GPSalt)   &&  (i < pos_m20GPSalt+3))   fprintf(stdout, "%s", COLOPT(col_GPSalt));
                    if ((i >= pos_m20GPSweek)  &&  (i < pos_m20GPSweek+2))  fprintf(stdout, "%s", COLOPT(col_GPSweek));
                    if ((i >= pos_m20GPSvE)    &&  (i < pos_m20GPSvE+2))    fprintf(stdout, "%s", COLOPT(col_GPSvel));
                    if ((i >= pos_m20GPSvN)    &&  (i < pos_m20GPSvN+2))    fprintf(stdout, "%s", COLOPT(col_GPSvel));
                    if ((i >= pos_m20GPSvU)    &&  (i < pos_m20GPSvU+2))    fprintf(stdout, "%s", COLOPT(col_GPSvel));
                    if ((i >= pos_m20SN)       &&  (i < pos_m20SN+3))       fprintf(stdout, "%s", COLOPT(col_SN));
                    if  (i == pos_m20CNT)                                   fprintf(stdout, "%s", COLOPT(col_CNT));
                    if (gpx->fwVer < 0x07) {
                        if ((i >= pos_m20BlkChk)   &&  (i < pos_m20BlkChk+2))   fprintf(stdout, "%s", COLOPT(col_Check));
                    } else {
                        if ((i >= pos_m20BlkChk+1) &&  (i < pos_m20BlkChk+2))   fprintf(stdout, "%s", COLOPT(col_Check));
                    }
                    if (i >= 0x02 && i <= 0x03)  fprintf(stdout, "%s", COLOPT(col_ptuU));
                    if (i >= 0x04 && i <= 0x05)  fprintf(stdout, "%s", COLOPT(col_ptuT));
                    if (i >= 0x06 && i <= 0x07)  fprintf(stdout, "%s", COLOPT(col_ptuTH));
                    if (i == 0x16 && gpx->fwVer >= 0x07 || i >= 0x24 && i <= 0x25)  fprintf(stdout, "%s", COLOPT(col_ptuP));
                }
                else if (gpx->type == t_M10)
                {
                    if ((i >= pos_m10GPSTOW)   &&  (i < pos_m10GPSTOW+4))   fprintf(stdout, "%s", COLOPT(col_GPSTOW));
                    if ((i >= pos_m10GPSlat)   &&  (i < pos_m10GPSlat+4))   fprintf(stdout, "%s", COLOPT(col_GPSlat));
                    if ((i >= pos_m10GPSlon)   &&  (i < pos_m10GPSlon+4))   fprintf(stdout, "%s", COLOPT(col_GPSlon));
                    if ((i >= pos_m10GPSalt)   &&  (i < pos_m10GPSalt+4))   fprintf(stdout, "%s", COLOPT(col_GPSalt));
                    if ((i >= pos_m10GPSweek)  &&  (i < pos_m10GPSweek+2))  fprintf(stdout, "%s", COLOPT(col_GPSweek));
                    if ((i >= pos_m10GPSvE)    &&  (i < pos_m10GPSvE+6))    fprintf(stdout, "%s", COLOPT(col_GPSvel));
                    if ((i >= pos_m10SN) && (i < pos_m10SN+5)) fprintf(stdout, "%s", COLOPT(col_SN));
                    if ((i == pos_m10CNT)) fprintf(stdout, "%s", COLOPT(col_CNT));
                }
                else if (gpx->type == t_M10plus)
                {
                    if ((i >= pos_gtopGPSlat)   &&  (i < pos_gtopGPSlat+4))   fprintf(stdout, "%s", COLOPT(col_GPSlat));
                    if ((i >= pos_gtopGPSlon)   &&  (i < pos_gtopGPSlon+4))   fprintf(stdout, "%s", COLOPT(col_GPSlon));
                    if ((i >= pos_gtopGPSalt)   &&  (i < pos_gtopGPSalt+3))   fprintf(stdout, "%s", COLOPT(col_GPSalt));
                    if ((i >= pos_gtopGPSvE)    &&  (i < pos_gtopGPSvE+6))    fprintf(stdout, "%s", COLOPT(col_GPSvel));
                    if ((i >= pos_gtopGPStime)  &&  (i < pos_gtopGPStime+3))  fprintf(stdout, "%s", COLOPT(col_GPSTOW));
                    if ((i >= pos_gtopGPSdate)  &&  (i < pos_gtopGPSdate+3))  fprintf(stdout, "%s", COLOPT(col_GPSweek));
                    if ((i >= pos_m10SN) && (i < pos_m10SN+5)) fprintf(stdout, "%s", COLOPT(col_SN));
                    if ((i == pos_m10CNT)) fprintf(stdout, "%s", COLOPT(col_CNT));
                }

                if ((i >= pos_check)  &&  (i < pos_check+2))  fprintf(stdout, "%s", COLOPT(col_Check));
                fprintf(stdout, "%02x", byte);
                fprintf(stdout, "%s", COLOPT(col_FRTXT));
            }
            if (gpx->option.vbs) {
                fprintf(stdout, " # %s%04x%s", COLOPT(col_Check), cs2, COLOPT(col_FRTXT));
                if (gpx->type == t_M20 && gpx->fwVer < 0x07) {
                    if      (bc > 0) fprintf(stdout, " %s(ok)%s", COLOPT(col_CSok), COLOPT(col_TXT));
                    else if (bc < 0) fprintf(stdout, " %s(oo)%s", COLOPT(col_CSoo), COLOPT(col_TXT));
                    else             fprintf(stdout, " %s(no)%s", COLOPT(col_CSno), COLOPT(col_TXT));
                }
                if (cs1 == cs2) fprintf(stdout, " %s[OK]%s", COLOPT(col_CSok), COLOPT(col_TXT));
                else            fprintf(stdout, " %s[NO]%s", COLOPT(col_CSno), COLOPT(col_TXT));
            }
            fprintf(stdout, "%s\n", COLOPT(ANSI_COLOR_RESET));
        }
        else {
            for (i = 0; i < flen+1; i++) {
                byte = gpx->frame_bytes[i];
                fprintf(stdout, "%02x", byte);
            }
            if (gpx->option.vbs) {
                fprintf(stdout, " # %04x", cs2);
                if (gpx->type == t_M20 && gpx->fwVer < 0x07) {
                    if      (bc > 0) fprintf(stdout, " (ok)");
                    else if (bc < 0) fprintf(stdout, " (oo)");
                    else             fprintf(stdout, " (no)");
                }
                if (cs1 == cs2) fprintf(stdout, " [OK]"); else fprintf(stdout, " [NO]");
            }
            fprintf(stdout, "\n");
        }
        if (gpx->option.slt   && gpx->type != t_M10dub) {
            print_pos(gpx, flen, bc, cs1 == cs2);
        }
    }
    else if (gpx->type == t_M10dub) {
        if (gpx->option.vbs == 3) {
            for (i = 0; i < FRAME_LEN+gpx->auxlen; i++) {
                byte = gpx->frame_bytes[i];
                fprintf(stdout, "%02x", byte);
            }
            if (cs1 == cs2) fprintf(stdout, " [OK]"); else fprintf(stdout, " [NO]");
            fprintf(stdout, "\n");
        }
    }
    else print_pos(gpx, flen, bc, cs1 == cs2);

    return ret;
}


#define m10GPSweekbit14 (pos_m10GPSweek*8+1)

static int M10bd9616(dsp_t *dsp, int bit) {
    double sps9616 = dsp->DFT.sr/(double)BAUD_M10;
    int sympos = m10GPSweekbit14*2*sps9616 + 0.5;
    int __pos = dsp->mv_pos + sympos + 1;
    int _pp = 4;
    for (_pp = -2; _pp < 3; _pp++) {
        if (dsp->bufs[(__pos+_pp-1 + dsp->M) % dsp->M] * dsp->bufs[(__pos+_pp + dsp->M) % dsp->M] < 0) break;
    }
    return (_pp >= 3);
}


int main(int argc, char **argv) {

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
    int spike = 0;
    int rawhex = 0;
    int cfreq = -1;

    float baudrate = -1;

    FILE *fp = NULL;
    char *fpname = NULL;

    int mbd = 0;
    int br_difs = 1;

    int k;

    int bit, bit0;
    int bitpos = 0;
    int bitQ;
    int pos;
    hsbit_t hsbit, hsbit1;


    int header_found = 0;

    float thres = 0.76;
    float _mv = 0.0;

    float lpIQ_bw = 24e3;

    int symlen = 2;
    int bitofs = 0;
    int shift = 0;

    pcm_t pcm = {0};
    dsp_t dsp = {0};

    hdb_t hdb = {0};

    gpx_t gpx = {0};


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
            fprintf(stderr, "       -r, --raw\n");
            fprintf(stderr, "       -c, --color\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            gpx.option.vbs = 1;
        }
        else if ( (strcmp(*argv, "-vv" ) == 0) ) gpx.option.vbs = 2;
        else if ( (strcmp(*argv, "-vvv") == 0) ) gpx.option.vbs = 3;
        else if ( (strcmp(*argv, "-r") == 0) || (strcmp(*argv, "--raw") == 0) ) {
            gpx.option.raw = 1;
        }
        else if ( (strcmp(*argv, "-i") == 0) || (strcmp(*argv, "--invert") == 0) ) {
            gpx.option.inv = 1;
        }
        else if ( (strcmp(*argv, "-c") == 0) || (strcmp(*argv, "--color") == 0) ) {
            gpx.option.col = 1;
        }
        else if ( (strcmp(*argv, "--br") == 0) ) {
            ++argv;
            if (*argv) {
                baudrate = atof(*argv);
                if (baudrate < 9000 || baudrate > 10000) baudrate = BAUD_M20;
            }
            else return -1;
        }
        else if ( (strcmp(*argv, "--ptu") == 0) ) {
            gpx.option.ptu = 1;
        }
        else if ( (strcmp(*argv, "--spike") == 0) ) {
            spike = 1;
        }
        else if   (strcmp(*argv, "--ch2") == 0) { sel_wavch = 1; }
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
            if (bw > 4.6 && bw < 48.0) lpIQ_bw = bw*1e3;
            option_lp |= LP_IQ;
        }
        else if   (strcmp(*argv, "--lpFM") == 0) { option_lp |= LP_FM; }
        else if   (strcmp(*argv, "--dc") == 0) { option_dc = 1; }
        else if   (strcmp(*argv, "--noLUT") == 0) { option_noLUT = 1; }
        else if   (strcmp(*argv, "--min") == 0) {
            option_min = 1;
        }
        else if   (strcmp(*argv, "--json") == 0) { gpx.option.jsn = 1; }
        else if   (strcmp(*argv, "--jsn_cfq") == 0) {
            int frq = -1;
            ++argv;
            if (*argv) frq = atoi(*argv); else return -1;
            if (frq < 300000000) frq = -1;
            cfreq = frq;
        }
        else if (strcmp(*argv, "--rawhex") == 0) { rawhex = 2; }
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

    if (cfreq > 0) gpx.jsn_freq = (cfreq+500)/1000;


    #ifdef EXT_FSK
    if (!option_softin) {
        option_softin = 1;
        fprintf(stderr, "reading float32 soft symbols\n");
    }
    #endif

    if (!rawhex) {
        if (!option_softin) {

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

            symlen = 2;

            dsp.fp = fp;
            dsp.sr = pcm.sr;
            dsp.bps = pcm.bps;
            dsp.nch = pcm.nch;
            dsp.ch = pcm.sel_ch;
            dsp.br = (float)BAUD_M20;
            dsp.sps = (float)dsp.sr/dsp.br;
            dsp.symlen = symlen;
            dsp.symhd = 1;
            dsp._spb = dsp.sps*symlen;
            dsp.hdr = rawheader;
            dsp.hdrlen = strlen(rawheader);
            dsp.BT = 1.8;
            dsp.h = 0.9;
            dsp.opt_iq = option_iq;
            dsp.opt_iqdc = option_iqdc;
            dsp.opt_lp = option_lp;
            dsp.lpIQ_bw = lpIQ_bw;
            dsp.lpFM_bw = 10e3;
            dsp.opt_dc = option_dc;
            dsp.opt_IFmin = option_min;

            if ( dsp.sps < 8 ) {
                fprintf(stderr, "note: sample rate low (%.1f sps)\n", dsp.sps);
            }

            if (baudrate > 0) {
                dsp.br = (float)baudrate;
                dsp.sps = (float)dsp.sr/dsp.br;
                fprintf(stderr, "sps corr: %.4f\n", dsp.sps);
            }



            k = init_buffers(&dsp);
            if ( k < 0 ) {
                fprintf(stderr, "error: init buffers\n");
                return -1;
            }

            bitofs += shift;
        }
        else {
            hdb.hdr = rawheader;
            hdb.len = strlen(rawheader);
            hdb.bufpos = -1;
            hdb.buf = NULL;
            hdb.ths = 0.8;
            hdb.sbuf = calloc(hdb.len, sizeof(float));
            if (hdb.sbuf == NULL) {
                fprintf(stderr, "error: malloc\n");
                return -1;
            }
        }


        while ( 1 )
        {
            if (option_softin) {
                header_found = find_softbinhead(fp, &hdb, &_mv, option_softin == 2);
            }
            else {
                header_found = find_header(&dsp, thres, 2, bitofs, dsp.opt_dc);
                _mv = dsp.mv;
            }

            if (header_found == EOF) break;

            if (_mv*(0.5-gpx.option.inv) < 0) {
                gpx.option.inv ^= 0x1;
            }

            if (header_found) {

                bitpos = 0;
                pos = 0;
                pos /= 2;
                bit0 = '0';

                while ( pos < BITFRAME_LEN+BITAUX_LEN ) {

                    if (option_softin) {
                        float s1 = 0.0;
                        float s2 = 0.0;
                        float s = 0.0;
                        bitQ = f32soft_read(fp, &s1, option_softin == 2);
                        if (bitQ != EOF) {
                            bitQ = f32soft_read(fp, &s2, option_softin == 2);
                            if (bitQ != EOF) {
                                s = s2-s1;
                                bit = (s>=0.0);
                            }
                        }
                    }
                    else {
                        float bl = -1;
                        if (option_iq >= 2) spike = 0;
                        if (option_iq > 2)  bl = 4.0;
                        bitQ = read_softbit2p(&dsp, &hsbit, 0, bitofs, bitpos, bl, spike, &hsbit1);
                        bit = hsbit.hb;
                        if (pos == m10GPSweekbit14) {
                            gpx.m10bd9616 = M10bd9616(&dsp, m10GPSweekbit14);
                        }
                    }
                    if ( bitQ == EOF ) { break; }

                    gpx.frame_bits[pos] = 0x31 ^ (bit0 ^ bit);
                    pos++;
                    bit0 = bit;
                    bitpos += 1;
                }
                gpx.frame_bits[pos] = '\0';

                mbd = print_frame(&gpx, pos, 1);
                if (gpx.frm_ok) {
                    br_difs = 0;
                }
                else if (dsp.br != mbd) {
                    br_difs += 1;
                    if (br_difs > 1) {
                        br_difs = 0;
                        if (mbd == BAUD_M10) {
                            dsp.br = BAUD_M10;
                            dsp.sps = (float)dsp.sr/dsp.br;
                        }
                        else if (mbd == BAUD_M20) {
                            dsp.br = BAUD_M20;
                            dsp.sps = (float)dsp.sr/dsp.br;
                        }
                    }
                }

                if (pos < BITFRAME_LEN) break;

                header_found = 0;

                if (gpx.option.vbs < 3) {
                    while ( bitpos < 5*BITFRAME_LEN ) {
                        if (option_softin) {
                            float s = 0.0;
                            bitQ = f32soft_read(fp, &s, option_softin == 2);
                        }
                        else {
                            bitQ = read_slbit(&dsp, &bit, 0, bitofs, bitpos, -1, 0);
                        }
                        if (bitQ == EOF) break;
                        bitpos++;
                    }
                }

                pos = 0;
            }
        }

        if (!option_softin) free_buffers(&dsp);
        else {
            if (hdb.buf) { free(hdb.buf); hdb.buf = NULL; }
        }
    }
    else
    {
        char buffer_rawhex[2*(FRAME_LEN+AUX_LEN)+12];
        char *pbuf = NULL, *buf_sp = NULL;
        ui8_t frmbyte;
        int frameofs = 0, len, i;

        while (1 > 0) {

            memset(buffer_rawhex, 0, 2*(FRAME_LEN+AUX_LEN)+12);
            pbuf = fgets(buffer_rawhex, 2*(FRAME_LEN+AUX_LEN)+12, fp);
            if (pbuf == NULL) break;
            buffer_rawhex[2*(FRAME_LEN+AUX_LEN)] = '\0';
            buf_sp = strchr(buffer_rawhex, ' ');
            if (buf_sp != NULL && buf_sp-buffer_rawhex < 2*(FRAME_LEN+AUX_LEN)) {
                buffer_rawhex[buf_sp-buffer_rawhex] = '\0';
                for (i = buf_sp-buffer_rawhex+1; i < 2*(FRAME_LEN+AUX_LEN); i++) buffer_rawhex[i] = '\0';
            }
            len = strlen(buffer_rawhex) / 2;
            if (len > pos_m10GPSweek+2) {
                for (i = 0; i < len; i++) {
                    sscanf(buffer_rawhex+2*i, "%2hhx", &frmbyte);
                    gpx.frame_bytes[frameofs+i] = frmbyte;
                }
                print_frame(&gpx, len*8, 0);
            }
        }
    }

    fclose(fp);

    return 0;
}

