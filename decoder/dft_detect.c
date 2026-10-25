
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <complex.h>

#ifdef HAVE_FFTW3
#include <fftw3.h>

static fftwf_plan dft_fwd_plan = NULL;

#define ALLOC_DFT_COMPLEX(n) ((float complex *)fftwf_alloc_complex(n))
#define FREE_DFT_COMPLEX(p)  fftwf_free(p)
#else
#define ALLOC_DFT_COMPLEX(n) ((float complex *)calloc((n), sizeof(float complex)))
#define FREE_DFT_COMPLEX(p)  free(p)
#endif

#ifndef M_PI
    #define M_PI  (3.1415926535897932384626433832795)
#endif

typedef unsigned char  ui8_t;
typedef unsigned short ui16_t;
typedef unsigned int   ui32_t;
typedef short i16_t;
typedef int   i32_t;


static int option_verbose = 0,  
           option_inv = 0,      
           option_min = 0,
           option_iq = 0,
           option_dc = 0,
           option_silent = 0,
           option_cont = 0,
           option_d2 = 0,
           option_pcmraw = 0,
           option_singleLpIQ = 0,
           wavloaded = 0;
static int wav_channel = 0;    


static char dfm_header[] = "10011010100110010101101001010101";
static char rs41_header[] = "00001000011011010101001110001000"
                            "01000100011010010100100000011111";
static char rs92_header[] =
                            "10100110011001101001"
                            "10100110011001101001"
                            "1010011001100110100110101010100110101001";
static char rd94rd41_header[] = "10100110010110101001"
                                "10010101011010010101"
                                "10101001010101010101"
                                "10011001010110101001";

static char lms6_header[] = "0101011000001000""0001110010010111"
                            "0001101010100111""0011110100111110";

static char mk2a_header[] = "0010100111""0010100111""0001001001""0010010101";

static char m10_header[] =
                                 "1001100110010100110010011001""1010";


static char meisei_header[] = "110011001101001101001101010100101010110010101010";

static char mrz_header[] = "1001100110011001""1001101010101010";

static char cf06ht03_header[] = "01010101"
                                "10110100""00101011""11000110";

static char imet54_header[] = "0000000001""0101010101""0001001001""0001001001";

static char mts01_header[] = "10101010""10101010"
                             "10110100""00101011";


static char imet_preamble[] =
                              "11110000111100001111000011110000"
                              "11110000111100001111000011110000";

static char imet1ab_header[] = "0000""11110000111100001111000011110000""1111"
                               "0000""10101100110010101100101010101100""1111";

static char imet1rs_header[] =
    "0000""1111""0000""1111""0000""1111"
    "0000""1111";



static char c34_preheader[] =
    "01010101010101010101010101010101";


static char weathex_header[] =
    "10101010""10101010""10101010"
    "00101101""11010100";

static char wxr2pn9_header[] =
    "10101010""10101010""10101010"
    "11000001""10010100";


static char windsond_header[] = "01010101010100101101";


typedef struct {
    int sps;
    int hLen;
    int L;
    char *header;
    float BT;
    float spb;
    float thres;
    int herrs;
    float complex *Fm;
    char *type;
    int tn;
    int lpFM;
    int lpIQ;
    float dc;
    float df;
} rsheader_t;

#define N_bwIQ 4
static float lpFM_bw[2] = { 4e3, 10e3 };
static float lpIQ_bw[N_bwIQ] = { 6e3, 12e3, 22e3, 200e3 };
static float set_lpIQ = 0.0;

#define tn_DFM        2
#define tn_RS41       3
#define tn_RS92       4
#define tn_M10        5
#define tn_M20        6
#define tn_LMS6       8
#define tn_MEISEI     9
#define tn_RD94RD41  10
#define tn_MRZ       12
#define tn_MTS01     13
#define tn_CF6GTH    14
#define tn_C34C50    15
#define tn_WXR301    16
#define tn_WXRpn9    17
#define tn_MK2LMS    18
#define tn_IMET5     24
#define tn_IMETa     25
#define tn_IMET4     26
#define tn_IMET1rs   28
#define tn_IMET1ab   29
#define tn_S1        30

static int idxIMETafsk = 17;
#define idxRS        18
#define idxI4        19
#define Nrs          20
static rsheader_t rs_hdr[Nrs] = {
    { 2500, 0, 0, dfm_header,      1.0, 0.0, 0.65, 2, NULL, "DFM9",     tn_DFM,      0, 1, 0.0, 0.0},
    { 4800, 0, 0, rs41_header,     0.5, 0.0, 0.70, 2, NULL, "RS41",     tn_RS41,     0, 1, 0.0, 0.0},
    { 4800, 0, 0, rs92_header,     0.5, 0.0, 0.70, 3, NULL, "RS92",     tn_RS92,     0, 1, 0.0, 0.0},
    { 4800, 0, 0, lms6_header,     1.0, 0.0, 0.60, 8, NULL, "LMS6",     tn_LMS6,     0, 1, 0.0, 0.0},
    { 4800, 0, 0, imet54_header,   0.5, 0.0, 0.80, 2, NULL, "IMET5",    tn_IMET5,    0, 1, 0.0, 0.0},
    { 9616, 0, 0, mk2a_header,     1.0, 0.0, 0.70, 2, NULL, "MK2LMS",   tn_MK2LMS,   1, 2, 0.0, 0.0},
    { 9608, 0, 0, m10_header,      1.0, 0.0, 0.76, 2, NULL, "M10",      tn_M10,      1, 2, 0.0, 0.0},
    { 2400, 0, 0, meisei_header,   1.0, 0.0, 0.70, 2, NULL, "MEISEI",   tn_MEISEI,   0, 2, 0.0, 0.0},
    { 4800, 0, 0, rd94rd41_header, 1.0, 0.0, 0.70, 2, NULL, "RD94RD41", tn_RD94RD41, 0, 1, 0.0, 0.0},
    { 2400, 0, 0, mrz_header,      1.5, 0.0, 0.80, 2, NULL, "MRZ",      tn_MRZ,      0, 1, 0.0, 0.0},
    { 1200, 0, 0, mts01_header,    1.0, 0.0, 0.65, 2, NULL, "MTS01",    tn_MTS01,    0, 0, 0.0, 0.0},
    { 2400, 0, 0, cf06ht03_header, 0.7, 0.0, 0.80, 2, NULL, "CF6GTH",   tn_CF6GTH,   0, 1, 0.0, 0.0},
    { 5800, 0, 0, c34_preheader,   1.5, 0.0, 0.80, 2, NULL, "C34C50",   tn_C34C50,   0, 2, 0.0, 0.0},
    { 4800, 0, 0, weathex_header,  1.0, 0.0, 0.65, 2, NULL, "WXR301",   tn_WXR301,   0, 3, 0.0, 0.0},
    { 5000, 0, 0, wxr2pn9_header,  1.0, 0.0, 0.65, 2, NULL, "WXRPN9",   tn_WXRpn9,   0, 3, 0.0, 0.0},
    { 2400, 0, 0, windsond_header, 1.0, 0.0, 0.80, 2, NULL, "S1",       tn_S1,       0, 1, 0.0, 0.0},
    { 9600, 0, 0, imet1ab_header,  1.0, 0.0, 0.80, 2, NULL, "IMET1AB",  tn_IMET1ab,  1, 3, 0.0, 0.0},
    { 9600, 0, 0, imet_preamble,   0.5, 0.0, 0.80, 4, NULL, "IMETafsk", tn_IMETa  ,  1, 1, 0.0, 0.0},
    { 9600, 0, 0, imet1rs_header,  0.5, 0.0, 0.80, 2, NULL, "IMET1RS",  tn_IMET1rs,  0, 3, 0.0, 0.0},
    { 9600, 0, 0, imet1rs_header,  0.5, 0.0, 0.80, 2, NULL, "IMET4",    tn_IMET4,    1, 1, 0.0, 0.0},
};


static int type_enabled[Nrs];
static int user_set_types = 0;


static int rs_detect2[Nrs];

static int rs_d2() {
    int tn = 0;
    for (tn = 0; tn < Nrs; tn++) {
        if ( rs_detect2[tn] > 1 ) break;
    }
    return tn;
}

static int reset_d2() {
    int n = 0;
    for (n = 0; n < Nrs; n++) rs_detect2[n] = 0;
    return 0;
}


#define FM_GAIN (0.8)

static int sr_base = 0;
static int sr_if = 0;

static int sample_rate = 0, bits_sample = 0, channels = 0;
static int wav_ch = 0;

static ui32_t sample_in, sample_out, delay;

static int M;

static float *buf_fm[N_bwIQ];
static float *bufs = NULL;

static char *rawbits = NULL;


static ui32_t dsp__sr_base;
static ui32_t dsp__dectaps;
static ui32_t dsp__sample_decX;
static int dsp__decM = 1;
static float complex *dsp__decXbuffer;
static float complex *dsp__decMbuf;
static float complex *dsp__ex;
static ui32_t dsp__lut_len;
static ui32_t dsp__sample_decM;

static float *ws_dec;
static double dsp__xlt_fq = 0.0;


static int LOG2N, N_DFT;

static float complex  *ew;

static float complex  *X, *Z, *cx;
static float *xn;

static float *xn_afsk;
static float *db_afsk;

static float *ws_lpFM[2];
static int dsp__lpFMtaps;
static float complex *Y;
static float complex *WS[2];
static float *ws_lpIQ[N_bwIQ];
static int dsp__lpIQtaps;
static float complex *lpIQ_buf;


#ifdef HAVE_FFTW3
static void dft_raw(float complex *Z) {
    fftwf_execute_dft(dft_fwd_plan, (fftwf_complex *)Z, (fftwf_complex *)Z);
}
#else
static void dft_raw(float complex *Z) {
    int s, l, l2, i, j, k;
    float complex  w1, w2, T;

    j = 1;
    for (i = 1; i < N_DFT; i++) {
        if (i < j) {
            T = Z[j-1];
            Z[j-1] = Z[i-1];
            Z[i-1] = T;
        }
        k = N_DFT/2;
        while (k < j) {
            j = j - k;
            k = k/2;
        }
        j = j + k;
    }

    for (s = 0; s < LOG2N; s++) {
        l2 = 1 << s;
        l  = l2 << 1;
        w1 = (float complex)1.0;
        w2 = ew[s];
        for (j = 1; j <= l2; j++) {
            for (i = j; i <= N_DFT; i += l) {
                k = i + l2;
                T = Z[k-1] * w1;
                Z[k-1] = Z[i-1] - T;
                Z[i-1] = Z[i-1] + T;
            }
            w1 = w1 * w2;
        }
    }
}
#endif

static void dft(float *x, float complex *Z) {
    int i;
    for (i = 0; i < N_DFT; i++)  Z[i] = (float complex)x[i];
    dft_raw(Z);
}

static void Nidft(float complex *Z, float complex *z) {
    int i;
    for (i = 0; i < N_DFT; i++)  z[i] = conj(Z[i]);
    dft_raw(z);
}

static float freq2bin(int f) {
    return  f * N_DFT / (float)sample_rate;
}

static float bin2freq(int k) {
    float fq = k / (float)N_DFT;
    if ( fq >= 0.5) fq -= 1.0;
    return fq*sample_rate;
}


static int getCorrDFT(int K, unsigned int pos, float *maxv, unsigned int *maxvpos, rsheader_t *rshd) {
    int i;
    int mp = -1;
    float mx = 0.0;
    float mx2 = 0.0;
    float re_cx = 0.0;
    double xnorm = 1.0;
    unsigned int mpos = 0;

    float dc = 0.0;
    rshd->dc = 0.0;

    if (K + rshd->L > N_DFT) return -1;

    if (pos == 0) pos = sample_out;

    bufs = buf_fm[rshd->lpIQ];

    for (i = 0; i < K+rshd->L; i++) xn[i] = bufs[(pos+M -(K+rshd->L-1) + i) % M];
    while (i < N_DFT) xn[i++] = 0.0;

    dft(xn, X);



    dc = 0.0;
    if (option_dc) {
        for (i=K-rshd->L; i<K+rshd->L;i++) dc += xn[i];
        dc /= 2.0*(float)rshd->L;
        X[0] -= N_DFT*dc  * 0.98;
    }
    rshd->dc = dc;

    if (option_iq) {
        for (i = 0; i < N_DFT; i++) X[i] *= WS[rshd->lpFM][i];
    }

    if (option_dc || option_iq) {
        Nidft(X, cx);
        for (i = 0; i < N_DFT; i++) xn[i] = creal(cx[i])/(float)N_DFT;
    }
    for (i = 0; i < N_DFT; i++) Z[i] = X[i] * rshd->Fm[i];
    Nidft(Z, cx);


    mx2 = 0.0;
    for (i = rshd->L-1; i < K+rshd->L; i++) {
        re_cx = creal(cx[i]);
        if (re_cx*re_cx > mx2) {
            mx = re_cx;
            mx2 = mx*mx;
            mp = i;
        }
    }
    if (mp < 0 || mp == rshd->L-1 || mp == K+rshd->L-1) return -4;

    mpos = pos - (K + rshd->L-1) + mp;

    xnorm = 0.0;
    for (i = 0; i < rshd->L; i++) xnorm += xn[mp-i]*xn[mp-i];
    xnorm = sqrt(xnorm);

    mx /= xnorm*N_DFT;

    if (option_iq) mpos -= dsp__lpFMtaps/2;

    *maxv = mx;
    *maxvpos = mpos;

    if (option_dc) {
        rshd->df = rshd->dc / (2.0*FM_GAIN*dsp__decM);
    }

    return mp;
}


static int findstr(char *buff, char *str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buff[(pos+i)%4] != str[i]) break;
    }
    return i;
}

static int read_wav_header(FILE *fp, int wav_channel) {
    char txt[4+1] = "\0\0\0\0";
    unsigned char dat[4];
    int byte, p=0;

    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "RIFF", 4) && strncmp(txt, "RF64", 4)) return -1;

    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "WAVE", 4))  return -1;

    for ( ; ; ) {
        if ( (byte=fgetc(fp)) == EOF ) return -1;
        txt[p % 4] = byte;
        p++; if (p==4) p=0;
        if (findstr(txt, "fmt ", p) == 4) break;
    }
    if (fread(dat, 1, 4, fp) < 4) return -1;
    if (fread(dat, 1, 2, fp) < 2) return -1;

    if (fread(dat, 1, 2, fp) < 2) return -1;
    channels = dat[0] + (dat[1] << 8);

    if (fread(dat, 1, 4, fp) < 4) return -1;
    memcpy(&sample_rate, dat, 4);

    if (fread(dat, 1, 4, fp) < 4) return -1;
    if (fread(dat, 1, 2, fp) < 2) return -1;

    if (fread(dat, 1, 2, fp) < 2) return -1;
    bits_sample = dat[0] + (dat[1] << 8);

    for ( ; ; ) {
        if ( (byte=fgetc(fp)) == EOF ) return -1;
        txt[p % 4] = byte;
        p++; if (p==4) p=0;
        if (findstr(txt, "data", p) == 4) break;
    }
    if (fread(dat, 1, 4, fp) < 4) return -1;


    fprintf(stderr, "sample_rate: %d\n", sample_rate);
    fprintf(stderr, "bits       : %d\n", bits_sample);
    fprintf(stderr, "channels   : %d\n", channels);

    if (wav_channel >= 0  &&  wav_channel < channels) wav_ch = wav_channel;
    else wav_ch = 0;

    if (bits_sample != 8 && bits_sample != 16 && bits_sample != 32) return -1;

    if (sample_rate == 900001) sample_rate -= 1;

    return 0;
}

static int f32read_sample(FILE *fp, float *s) {
    int i;
    unsigned int word = 0;
    short *b = (short*)&word;
    float *f = (float*)&word;

    for (i = 0; i < channels; i++) {

        if (fread( &word, bits_sample/8, 1, fp) != 1) return EOF;

        if (i == wav_ch) {

            if (bits_sample == 32) {
                *s = *f;
            }
            else {
                if (bits_sample ==  8) { *b -= 128; }
                *s = *b/128.0;
                if (bits_sample == 16) { *s /= 256.0; }
            }
        }
    }

    return 0;
}


typedef struct {
    double sumIQx;
    double sumIQy;
    float avgIQx;
    float avgIQy;
    ui32_t cnt;
    ui32_t maxcnt;
} iq_dc_t;
static iq_dc_t IQdc;

static int f32read_csample(FILE *fp, float complex *z) {

    float x, y;

    if (bits_sample == 32) {
        float f[2];
        if (fread( f, bits_sample/8, 2, fp) != 2) return EOF;
        x = f[0];
        y = f[1];
    }
    else if (bits_sample == 16) {
        short b[2];
        if (fread( b, bits_sample/8, 2, fp) != 2) return EOF;
        x = b[0]/32768.0;
        y = b[1]/32768.0;
    }
    else {
        ui8_t u[2];
        if (fread( u, bits_sample/8, 2, fp) != 2) return EOF;
        x = (u[0]-128)/128.0;
        y = (u[1]-128)/128.0;
    }

    *z = (x - IQdc.avgIQx) + I*(y - IQdc.avgIQy);

    IQdc.sumIQx += x;
    IQdc.sumIQy += y;
    IQdc.cnt += 1;
    if (IQdc.cnt == IQdc.maxcnt) {
        IQdc.avgIQx = IQdc.sumIQx/(float)IQdc.maxcnt;
        IQdc.avgIQy = IQdc.sumIQy/(float)IQdc.maxcnt;
        IQdc.sumIQx = 0; IQdc.sumIQy = 0; IQdc.cnt = 0;
    }

    return 0;
}

static int f32read_cblock(FILE *fp) {

    int n;
    int len;
    float x, y;

    len = dsp__decM;

    if (bits_sample == 8) {
        ui8_t u[2*dsp__decM];
        len = fread( u, bits_sample/8, 2*dsp__decM, fp) / 2;
        for (n = 0; n < len; n++) {
            x = (u[2*n  ]-128)/128.0;
            y = (u[2*n+1]-128)/128.0;
            dsp__decMbuf[n] = (x-IQdc.avgIQx) + I*(y-IQdc.avgIQy);
            IQdc.sumIQx += x;
            IQdc.sumIQy += y;
            IQdc.cnt += 1;
            if (IQdc.cnt == IQdc.maxcnt) {
                IQdc.avgIQx = IQdc.sumIQx/(float)IQdc.maxcnt;
                IQdc.avgIQy = IQdc.sumIQy/(float)IQdc.maxcnt;
                IQdc.sumIQx = 0; IQdc.sumIQy = 0; IQdc.cnt = 0;
            }
        }
    }
    else if (bits_sample == 16) {
        short b[2*dsp__decM];
        len = fread( b, bits_sample/8, 2*dsp__decM, fp) / 2;
        for (n = 0; n < len; n++) {
            x = b[2*n  ]/32768.0;
            y = b[2*n+1]/32768.0;
            dsp__decMbuf[n] = (x-IQdc.avgIQx) + I*(y-IQdc.avgIQy);
            IQdc.sumIQx += x;
            IQdc.sumIQy += y;
            IQdc.cnt += 1;
            if (IQdc.cnt == IQdc.maxcnt) {
                IQdc.avgIQx = IQdc.sumIQx/(float)IQdc.maxcnt;
                IQdc.avgIQy = IQdc.sumIQy/(float)IQdc.maxcnt;
                IQdc.sumIQx = 0; IQdc.sumIQy = 0; IQdc.cnt = 0;
            }
        }
    }
    else {
        float f[2*dsp__decM];
        len = fread( f, bits_sample/8, 2*dsp__decM, fp) / 2;
        for (n = 0; n < len; n++) {
            x = f[2*n];
            y = f[2*n+1];
            dsp__decMbuf[n] = (x-IQdc.avgIQx) + I*(y-IQdc.avgIQy);
            IQdc.sumIQx += x;
            IQdc.sumIQy += y;
            IQdc.cnt += 1;
            if (IQdc.cnt == IQdc.maxcnt) {
                IQdc.avgIQx = IQdc.sumIQx/(float)IQdc.maxcnt;
                IQdc.avgIQy = IQdc.sumIQy/(float)IQdc.maxcnt;
                IQdc.sumIQx = 0; IQdc.sumIQy = 0; IQdc.cnt = 0;
            }
        }
    }

    return len;
}

static double sinc(double x) {
    double y;
    if (x == 0) y = 1;
    else y = sin(M_PI*x)/(M_PI*x);
    return y;
}

static int lowpass_init(float f, int taps, float **pws) {
    double *h, *w;
    double norm = 0;
    int n;
    float *ws = NULL;

    if (taps % 2 == 0) taps++;

    if ( taps < 1 ) taps = 1;

    h = (double*)calloc( taps+1, sizeof(double)); if (h == NULL) return -1;
    w = (double*)calloc( taps+1, sizeof(double)); if (w == NULL) return -1;
    ws = (float*)calloc( 2*taps+1, sizeof(float)); if (ws == NULL) return -1;

    for (n = 0; n < taps; n++) {
        w[n] = 7938/18608.0 - 9240/18608.0*cos(2*M_PI*n/(taps-1)) + 1430/18608.0*cos(4*M_PI*n/(taps-1));
        h[n] = 2*f*sinc(2*f*(n-(taps-1)/2));
        ws[n] = w[n]*h[n];
        norm += ws[n];
    }
    for (n = 0; n < taps; n++) {
        ws[n] /= norm;
    }

    for (n = 0; n < taps; n++) ws[taps+n] = ws[n];

    *pws = ws;

    free(h); h = NULL;
    free(w); w = NULL;

    return taps;
}

static float complex lowpass0(float complex buffer[], ui32_t sample, ui32_t taps, float *ws) {
    ui32_t n;
    double complex w = 0;
    for (n = 0; n < taps; n++) {
        w += buffer[(sample+n)%taps]*ws[taps-1-n];
    }
    return (float complex)w;
}
static float complex lowpass(float complex buffer[], ui32_t sample, ui32_t taps, float *ws) {
    float complex w = 0;
    int n;
    int S = taps - (sample % taps);
    for (n = 0; n < taps; n++) {
        w += buffer[n]*ws[S+n];
    }
    return w;
}
static float complex lowpass2(float complex buffer[], ui32_t sample, ui32_t taps, float *ws) {
    float complex w = 0;
    int n;
    int s = sample % taps;
    int S1 = s;
    int S1N = S1-taps;
    int n0 = taps-s;
    for (n = 0; n < n0; n++) {
        w += buffer[S1+n]*ws[n];
    }
    for (n = n0; n < taps; n++) {
        w += buffer[S1N+n]*ws[n];
    }
    return w;
}


static int f32buf_sample(FILE *fp, int inv) {
    float _s = 0.0;
    float s[N_bwIQ];
    static float complex z0_fm0;
    static float complex z0_fm1;
    static float complex z0_fm2;
    static float complex z0;
    float complex z_fm0=0, z_fm1=0, z_fm2=0;
    float complex z, w;
    double gain = FM_GAIN;
    int i;

    if (option_iq)
    {
        if (option_iq == 5) {
            int j;
            if ( f32read_cblock(fp) < dsp__decM ) return EOF;
            for (j = 0; j < dsp__decM; j++) {
                dsp__decXbuffer[dsp__sample_decX] = dsp__decMbuf[j] * dsp__ex[dsp__sample_decM];
                dsp__sample_decM += 1; if (dsp__sample_decM >= dsp__lut_len) dsp__sample_decM = 0;
                dsp__sample_decX += 1; if (dsp__sample_decX >= dsp__dectaps) dsp__sample_decX = 0;
            }
            z = lowpass(dsp__decXbuffer, dsp__sample_decX, dsp__dectaps, ws_dec);

        }
        else if ( f32read_csample(fp, &z) == EOF ) return EOF;

        lpIQ_buf[sample_in % dsp__lpIQtaps] = z;
        z_fm0 = lowpass(lpIQ_buf, sample_in+1, dsp__lpIQtaps, ws_lpIQ[0]);
        if (option_singleLpIQ) {
            z_fm1 = z_fm0;
            z_fm2 = z_fm0;
        }
        else {
            z_fm1 = lowpass(lpIQ_buf, sample_in+1, dsp__lpIQtaps, ws_lpIQ[1]);
            z_fm2 = lowpass(lpIQ_buf, sample_in+1, dsp__lpIQtaps, ws_lpIQ[2]);
        }
        w = z_fm0 * conj(z0_fm0);
        s[0] = gain * carg(w)/M_PI;
        z0_fm0 = z_fm0;

        if (option_singleLpIQ) {
            s[1] = s[0]; z0_fm1 = z_fm1;
            s[2] = s[0]; z0_fm2 = z_fm2;
        }
        else {
            w = z_fm1 * conj(z0_fm1);
            s[1] = gain * carg(w)/M_PI;
            z0_fm1 = z_fm1;

            w = z_fm2 * conj(z0_fm2);
            s[2] = gain * carg(w)/M_PI;
            z0_fm2 = z_fm2;
        }

        w = z * conj(z0);
        s[3] = gain * carg(w)/M_PI;
        z0 = z;
    }
    else
    {
        if (f32read_sample(fp, &_s) == EOF) return EOF;
        for (i = 0; i < N_bwIQ; i++) s[i] = _s;
    }

    for (i = 0; i < N_bwIQ; i++) {
        if (inv) s[i]= -s[i];
        buf_fm[i][sample_in % M] = s[i];
    }


    sample_out = sample_in - delay;

    sample_in += 1;

    return 0;
}

static int read_bufbit(int symlen, char *bits, unsigned int mvp, int reset, float dc, rsheader_t *rshd) {

    static unsigned int rcount;
    static float rbitgrenze;

    double sum = 0.0;

    bufs = buf_fm[rshd->lpIQ];

    if (reset) {
        rcount = 0;
        rbitgrenze = 0;
    }


    rbitgrenze += rshd->spb;
    do {
        sum += bufs[(rcount + mvp + M) % M] - dc;
        rcount++;
    } while (rcount < rbitgrenze);

    if (symlen == 2) {
        rbitgrenze += rshd->spb;
        do {
            sum -= bufs[(rcount + mvp + M) % M] - dc;
            rcount++;
        } while (rcount < rbitgrenze);
    }


    if (symlen != 2) {
        if (sum >= 0) *bits = '1';
        else          *bits = '0';
    }
    else {
        if (sum >= 0) strncpy(bits, "10", 2);
        else          strncpy(bits, "01", 2);
    }

    return 0;
}

static int headcmp(int symlen, unsigned int mvp, int inv, rsheader_t *rshd) {
    int errs = 0;
    int pos;
    int step = 1;
    int len = 0;
    char sign = 0;
    float dc = 0.0;

    if (option_dc)
    {
        dc = rshd->dc;
    }

    if (symlen != 1) step = 2;
    if (inv) sign=1;

    len = rshd->hLen;
    for (pos = 0; pos < len; pos += step) {
        read_bufbit(symlen, rawbits+pos, mvp+1-(int)(rshd->hLen*rshd->spb), pos==0, dc, rshd);
    }
    rawbits[pos] = '\0';

    while (len > 0) {
        if ((rawbits[len-1]^sign) != rshd->header[len-1]) errs += 1;
        len--;
    }

    return errs;
}


static ui8_t bits2byte(char *bitstr) {
    int i, bit, d, byteval;
    int bitpos;

    bitpos = 0;
    byteval = 0;
    d = 1;
    for (i = 0; i < 8; i++) {
        bit=*(bitstr+bitpos+7-i);
        if         (bit == '1')    byteval += d;
        else    byteval += 0;
        d <<= 1;
    }

    return byteval & 0xFF;
}

static int hw(ui8_t byte) {
    int i;
    int d = 0;
    for (i = 0; i < 8; i++) {
        d += (byte & 1);
        byte >>= 1;
    }
    return d;
}

static ui32_t frm_M10(unsigned int mvp, int inv, rsheader_t *rshd) {
    float dc = 0.0;
    int pos2;
    char bit0 = '0';
    char mb[2];
    char frmbit[16+1];
    ui8_t b[2];
    ui32_t bytes;

    int ofs = (strlen(rshd->header) - 28)/2;

    if (ofs < 0 || ofs > 8) ofs = 0;

    if (option_dc) dc = rshd->dc;

    bit0 = 0x30 + (inv > 0);
    for (pos2 = 0; pos2 < 16; pos2 += 1) {
        if (pos2 < ofs) {
            mb[0] = rshd->header[28+2*pos2] ^ (inv>0);
        }
        else {
            read_bufbit(2, mb, mvp, pos2==ofs, dc, rshd);
        }
        frmbit[pos2] = 0x31 ^ (bit0 ^ mb[0]);
        bit0 = mb[0];
    }
    frmbit[pos2] = '\0';

    b[0] = bits2byte(frmbit);
    b[1] = bits2byte(frmbit+8);
    bytes = (b[0]<<8) | b[1];

    return bytes;
}


#define IF_SAMPLE_RATE      48000
#define IF_SAMPLE_RATE_MIN  32000

#define SQRT2 1.4142135624

static double Q(double x) {
    return 0.5 - 0.5*erf(x/SQRT2);
}
static double pulse(double t, double sigma) {
    return Q((t-0.5)/sigma) - Q((t+0.5)/sigma);
}


static double norm2_match(float *match, int n) {
    int i;
    double x, y = 0.0;
    for (i = 0; i < n; i++) {
        x = match[i];
        y += x*x;
    }
    return y;
}

static int init_buffers() {

    int i, j, pos;
    double t;
    double b0, b1, b2, b;
    float normMatch;

    int p2 = 1;
    int K, L;
    int n, k;
    float *match = NULL;
    float *m = NULL;

    double BT = 0.5;
    double sigma = sqrt(log(2)) / (2*M_PI*BT);

    char *bits = NULL;
    float spb = 0.0;

    int hLen = 0;
    int Lmax = 0;

    sr_base = sample_rate;
    sr_if = sample_rate;


    if (option_iq == 5)
    {
        int IF_sr = IF_SAMPLE_RATE;
        int decM = 1;
        float f_lp;
        float t_bw;
        int taps;
        int wideIF = 0;

        if (set_lpIQ > IF_sr) IF_sr = set_lpIQ;

        wideIF = IF_sr > 60e3;

        sr_base = sample_rate;

        if (option_min) IF_sr = IF_SAMPLE_RATE_MIN;
        if (IF_sr > sr_base) IF_sr = sr_base;
        if (IF_sr < sr_base) {
            while (sr_base % IF_sr) IF_sr += 1;
            decM = sr_base / IF_sr;
        }

        f_lp = (IF_sr+20e3)/(4.0*sr_base);
        t_bw = (IF_sr-20e3) ;
        if (wideIF) {
            f_lp = (IF_sr+60e3)/(4.0*sr_base);
            t_bw = (IF_sr-60e3) ;
        }
        else
        if (option_min) {
            t_bw = (IF_sr-12e3);
        }
        if (t_bw < 0) t_bw = 10e3;
        t_bw /= sr_base;
        taps = 4.0/t_bw; if (taps%2==0) taps++;

        taps = lowpass_init(f_lp, taps, &ws_dec);
        if (taps < 0) return -1;
        dsp__dectaps = taps;

        dsp__sr_base = sr_base;
        sample_rate = IF_sr;
        dsp__decM = decM;

        sr_if = IF_sr;

        fprintf(stderr, "IF: %d\n", IF_sr);
        fprintf(stderr, "dec: %d\n", decM);
    }
    if (option_iq == 5)
    {
        int W = 2*8;
        int d = 1;
        int freq = (int)( dsp__xlt_fq * (double)dsp__sr_base + 0.5);
        int freq0 = freq;
        double f0 = freq0 / (double)dsp__sr_base;

        for (d = W; d > 0; d--) {
            if (dsp__sr_base % d == 0) break;
        }
        if (d == 0) d = 1;

        for (k = 0; k < W/2; k++) {
            if ((freq+k) % d == 0) {
                freq0 = freq + k;
                break;
            }
            if ((freq-k) % d == 0) {
                freq0 = freq - k;
                break;
            }
        }

        dsp__lut_len = dsp__sr_base / d;
        f0 = freq0 / (double)dsp__sr_base;

        dsp__ex = calloc(dsp__lut_len+1, sizeof(float complex));
        if (dsp__ex == NULL) return -1;
        for (n = 0; n < dsp__lut_len; n++) {
            t = f0*(double)n;
            dsp__ex[n] = cexp(t*2*M_PI*I);
        }


        dsp__decXbuffer = calloc( dsp__dectaps+1, sizeof(float complex));
        if (dsp__decXbuffer == NULL) return -1;

        dsp__decMbuf = calloc( dsp__decM+1, sizeof(float complex));
        if (dsp__decMbuf == NULL) return -1;
    }


    if (option_iq)
    {
        float f_lp;
        int taps;

        taps = 4*sample_rate/2e3; if (taps%2==0) taps++;
        f_lp = lpFM_bw[0]/(float)sample_rate;
        taps = lowpass_init(f_lp, taps, &ws_lpFM[0]); if (taps < 0) return -1;
        f_lp = lpFM_bw[1]/(float)sample_rate;
        taps = lowpass_init(f_lp, taps, &ws_lpFM[1]); if (taps < 0) return -1;
        dsp__lpFMtaps = taps;

        taps = 4*sample_rate/4e3; if (taps%2==0) taps++;
        if (set_lpIQ > 100.0) {
            lpIQ_bw[0] = set_lpIQ;
            lpIQ_bw[1] = set_lpIQ;
            lpIQ_bw[2] = set_lpIQ;
            option_singleLpIQ = 1;
        }
        f_lp = lpIQ_bw[0]/(float)sample_rate/2.0;
        taps = lowpass_init(f_lp, taps, &ws_lpIQ[0]); if (taps < 0) return -1;
        f_lp = lpIQ_bw[1]/(float)sample_rate/2.0;
        taps = lowpass_init(f_lp, taps, &ws_lpIQ[1]); if (taps < 0) return -1;
        f_lp = lpIQ_bw[2]/(float)sample_rate/2.0;
        taps = lowpass_init(f_lp, taps, &ws_lpIQ[2]); if (taps < 0) return -1;
        dsp__lpIQtaps = taps;
        lpIQ_buf = calloc( dsp__lpIQtaps+3, sizeof(float complex));
        if (lpIQ_buf == NULL) return -1;

    }

    memset(&IQdc, 0, sizeof(IQdc));
    IQdc.maxcnt = sample_rate/32;
    if (dsp__decM > 1) IQdc.maxcnt *= dsp__decM;

    for (j = 0; j < Nrs; j++) {
        rs_hdr[j].spb = sample_rate/(float)rs_hdr[j].sps;
        rs_hdr[j].hLen = strlen(rs_hdr[j].header);
        rs_hdr[j].L = rs_hdr[j].hLen * rs_hdr[j].spb + 0.5;

        if (rs_hdr[j].hLen > hLen) hLen = rs_hdr[j].hLen;
        if (rs_hdr[j].L > Lmax) Lmax = rs_hdr[j].L;
    }

    L = 2*Lmax;

    M = 3*L;

    sample_in = 0;

    p2 = 1;
    while (p2 < M) p2 <<= 1;
    while (p2 < 0x2000) p2 <<= 1;
    N_DFT = p2;
    K = N_DFT - L;
    LOG2N = log(N_DFT)/log(2)+0.1;

    delay = L/16;
    M = N_DFT + delay + 8;


    rawbits = (char *)calloc( hLen+1, sizeof(char)); if (rawbits == NULL) return -100;
    for (j = 0; j < N_bwIQ; j++) {
        buf_fm[j]  = (float *)calloc( M+1, sizeof(float)); if (buf_fm[j]  == NULL) return -100;
    }
    bufs = buf_fm[N_bwIQ-1];


    xn = calloc(N_DFT+1, sizeof(float));  if (xn == NULL) return -1;

    db_afsk = calloc(N_DFT+1, sizeof(float));  if (db_afsk == NULL) return -1;
    xn_afsk = calloc(N_DFT+1, sizeof(float));  if (xn_afsk == NULL) return -1;

    ew = calloc(LOG2N+1, sizeof(float complex));  if (ew == NULL) return -1;
    X  = ALLOC_DFT_COMPLEX(N_DFT+1);  if (X  == NULL) return -1;
    Z  = ALLOC_DFT_COMPLEX(N_DFT+1);  if (Z  == NULL) return -1;
    cx = ALLOC_DFT_COMPLEX(N_DFT+1);  if (cx == NULL) return -1;

    for (n = 0; n < LOG2N; n++) {
        k = 1 << n;
        ew[n] = cexp(-I*M_PI/(float)k);
    }

#ifdef HAVE_FFTW3
    {
        fftwf_complex *scratch = fftwf_alloc_complex(N_DFT);
        if (scratch == NULL) return -1;
        dft_fwd_plan = fftwf_plan_dft_1d(N_DFT, scratch, scratch,
                                         FFTW_FORWARD, FFTW_ESTIMATE);
        fftwf_free(scratch);
        if (dft_fwd_plan == NULL) return -1;
    }
#endif

    match = (float *)calloc( L+1, sizeof(float)); if (match == NULL) return -1;
    m = (float *)calloc(N_DFT+1, sizeof(float));  if (m  == NULL) return -1;


    for (j = 0; j < idxRS; j++)
    {
        rs_hdr[j].Fm = ALLOC_DFT_COMPLEX(N_DFT+1);  if (rs_hdr[j].Fm == NULL) return -1;
        bits = rs_hdr[j].header;
        spb = rs_hdr[j].spb;
        sigma = sqrt(log(2)) / (2*M_PI*rs_hdr[j].BT);

        for (i = 0; i < rs_hdr[j].L; i++) {

            pos = i/spb;
            t = (i - pos*spb)/spb - 0.5;

            b1 = ((bits[pos] & 0x1) - 0.5)*2.0;
            b = b1*pulse(t, sigma);

            if (pos > 0) {
                b0 = ((bits[pos-1] & 0x1) - 0.5)*2.0;
                b += b0*pulse(t+1, sigma);
            }

            if (pos < hLen-1) {
                b2 = ((bits[pos+1] & 0x1) - 0.5)*2.0;
                b += b2*pulse(t-1, sigma);
            }

            match[i] = b;
        }

        normMatch = sqrt(norm2_match(match, rs_hdr[j].L));
        for (i = 0; i < rs_hdr[j].L; i++) {
            match[i] /= normMatch;
        }

        for (i = 0; i < rs_hdr[j].L; i++) m[rs_hdr[j].L-1 - i] = match[i];
        while (i < N_DFT) m[i++] = 0.0;
        dft(m, rs_hdr[j].Fm);

    }


    if (option_iq)
    {
        for (j = 0; j < 2; j++) {
            WS[j] = ALLOC_DFT_COMPLEX(N_DFT+1);  if (WS[j] == NULL) return -1;
            for (i = 0; i < dsp__lpFMtaps; i++) m[i] = ws_lpFM[j][i];
            while (i < N_DFT) m[i++] = 0.0;
            dft(m, WS[j]);
        }
        Y = (float complex *)calloc(N_DFT+1, sizeof(float complex));  if (Y == NULL) return -1;
    }


    free(match); match = NULL;
    free(m); m = NULL;

    return K;
}

static int free_buffers() {
    int j;

    for (j = 0; j < N_bwIQ; j++) {
        if (buf_fm[j])  { free(buf_fm[j]);  buf_fm[j]  = NULL; }
    }

    if (rawbits) { free(rawbits); rawbits = NULL; }

    if (xn) { free(xn); xn = NULL; }
    if (xn_afsk) { free(xn_afsk); xn_afsk = NULL; }
    if (db_afsk) { free(db_afsk); db_afsk = NULL; }
    if (ew) { free(ew); ew = NULL; }
    if (X)  { FREE_DFT_COMPLEX(X);  X  = NULL; }
    if (Z)  { FREE_DFT_COMPLEX(Z);  Z  = NULL; }
    if (cx) { FREE_DFT_COMPLEX(cx); cx = NULL; }

#ifdef HAVE_FFTW3
    if (dft_fwd_plan) { fftwf_destroy_plan(dft_fwd_plan); dft_fwd_plan = NULL; }
#endif

    for (j = 0; j < idxRS; j++) {
        if (rs_hdr[j].Fm) { FREE_DFT_COMPLEX(rs_hdr[j].Fm); rs_hdr[j].Fm = NULL; }
    }



    if (option_iq == 5)
    {
        if (dsp__decXbuffer) { free(dsp__decXbuffer); dsp__decXbuffer = NULL; }
        if (dsp__decMbuf) { free(dsp__decMbuf); dsp__decMbuf = NULL; }
        if (dsp__ex) { free(dsp__ex); dsp__ex = NULL; }

    }

    if (option_iq) {
        for (j = 0; j < 2; j++) {
            if (ws_lpFM[j]) { free(ws_lpFM[j]); ws_lpFM[j] = NULL; }
            if (WS[j]) { FREE_DFT_COMPLEX(WS[j]); WS[j] = NULL; }
        }
        if (Y) { free(Y); Y = NULL; }

        for (j = 0; j < N_bwIQ-1; j++) {
            if (ws_lpIQ[j]) { free(ws_lpIQ[j]); ws_lpIQ[j] = NULL; }
        }
        if (lpIQ_buf) { free(lpIQ_buf); lpIQ_buf = NULL; }
    }


    return 0;
}



int main(int argc, char **argv) {

    FILE *fp = NULL;
    char *fpname = NULL;

    int j;
    int k, K;
    float mv[Nrs];
    unsigned int mv_pos[Nrs], mv0_pos[Nrs];
    int mp[Nrs];

    int header_found = 0;
    int herrs;
    float thres = 0.76;
    float tl = -1.0;

    int j_max;
    float mv_max;

    int d2_tn = Nrs;

    ui32_t frm2_M10M20 = 0;

    int n, m, mf;
    int D = 0;
    int nD, nT, T;
    float df;
    float pow800, pow2200, pow2400, pow1200;
    int bin800, bin2200, bin2400, bin1200;


#ifdef CYGWIN
    _setmode(fileno(stdin), _O_BINARY);
#endif
    setbuf(stdout, NULL);

    for (j = 0; j < Nrs; j++) {
        type_enabled[j] = 1;
        if ( strncmp(rs_hdr[j].type, "IMETafsk", 8) == 0 ) idxIMETafsk = j;
    }

    fpname = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] audio.wav\n", fpname);
            fprintf(stderr, "  options:\n");
            fprintf(stderr, "       -v          (verbose)\n");
            fprintf(stderr, "       -c          (continuous)\n");
            fprintf(stderr, "       --iq        (IF iq-data)\n");
            fprintf(stderr, "       --IQ <fq>   (baseband IQ at fq)\n");
            fprintf(stderr, "       --bw <kHz>  (set IQ filter bw/kHz)\n");
            fprintf(stderr, "       --types <list>  (comma-separated rs_hdr type names to scan,\n");
            fprintf(stderr, "                        e.g. DFM9,RS41,RS92; default: scan all)\n");
            fprintf(stderr, "       --exclude-types <list>  (comma-separated list)\n");
            fprintf(stderr, "  types:");
            for (j = 0; j < Nrs; j++) {
                if (j == idxIMETafsk) continue;
                fprintf(stderr, "%s", j % 8 ? " " : "\n       ");
                fprintf(stderr, "%s", rs_hdr[j].type);
                if (strncmp(rs_hdr[j].type, "DFM9", 4) == 0) fprintf(stderr, " (== DFM6, DFM17)");
                if (strncmp(rs_hdr[j].type, "M10", 4) == 0) fprintf(stderr, " (== M20)");
                fprintf(stderr, "%s", j < Nrs-1 ? "," : "\n");
            }
            fprintf(stderr, "       (IMET1RS=IMET4)\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        else if ( (strcmp(*argv, "--iq") == 0) ) { option_iq = 1; }
        else if   (strcmp(*argv, "--IQ") == 0) {
            double fq = 0.0;
            ++argv;
            if (*argv) fq = atof(*argv);
            else return -1;
            if (fq < -0.5) fq = -0.5;
            if (fq >  0.5) fq =  0.5;
            dsp__xlt_fq = -fq;
            option_iq = 5;
        }
        else if   (strcmp(*argv, "--bw") == 0) {
            double bw_kHz = 0.0;
            ++argv;
            if (*argv) bw_kHz = atof(*argv); else return -1;
            if (bw_kHz < 1.0) bw_kHz = 0.0;
            set_lpIQ = bw_kHz * 1e3;
        }
        else if ( (strcmp(*argv, "--types") == 0) || (strcmp(*argv, "--exclude-types") == 0) ) {
            int types_exclude = (strcmp(*argv, "--exclude-types") == 0) ? 1 : 0;
            ++argv;
            if (*argv == NULL || (*argv)[0] == '\0') {
                fprintf(stderr, "error: --types/--exclude-types require a non-empty comma-separated list\n");
                return -1;
            }
            user_set_types += 1;
            if (user_set_types > 1) {
                fprintf(stderr, "error: too many --types/--exclude-types\n");
                return -1;
            }
            int n;
            for (n = 0; n < Nrs; n++) type_enabled[n] = types_exclude ? 1 : 0;
            const char *p = *argv;
            const char *start = p;
            while (1) {
                if (*p == ',' || *p == '\0') {
                    char token[64];
                    size_t len = (size_t)(p - start);
                    if (len >= sizeof(token)) {
                        fprintf(stderr, "warning: --[exclude-]types: token too long, ignored\n");
                    }
                    else {
                        memcpy(token, start, len);
                        token[len] = '\0';
                        char *t = token;
                        while (*t == ' ' || *t == '\t') t++;
                        char *e = token + strlen(token);
                        while (e > t && (*(e-1) == ' ' || *(e-1) == '\t')) { e--; *e = '\0'; }
                        if (*t != '\0') {
                            int matched = 0;
                            for (n = 0; n < Nrs; n++) {
                                if (strcmp(rs_hdr[n].type, t) == 0) {
                                    type_enabled[n] = types_exclude ? 0 : 1;
                                    matched = 1;
                                    if (n > idxIMETafsk) type_enabled[idxIMETafsk] = types_exclude ? 0 : 1;
                                    break;
                                }
                            }
                            if (!matched) {
                                fprintf(stderr, "warning: --[exclude-]types: unknown sonde type '%s', ignored\n", t);
                            }
                        }
                    }
                    if (*p == '\0') break;
                    start = p + 1;
                }
                p++;
            }
            int any = 0;
            for (n = 0; n < Nrs; n++) {
                if (type_enabled[n]) { any = 1; break; }
            }
            if (!any) {
                fprintf(stderr, "warning: --[exclude-]types: no valid types, scanning all\n");
                for (n = 0; n < Nrs; n++) type_enabled[n] = 1;
            }
        }
        else if ( (strcmp(*argv, "--dc") == 0) ) { option_dc = 1; }
        else if   (strcmp(*argv, "--min") == 0) {
            option_min = 1;
        }
        else if ( (strcmp(*argv, "-L") == 0) ) {
            lpIQ_bw[0] = 20e3;
            lpIQ_bw[1] = 32e3;
            lpIQ_bw[2] = 200e3;
            lpIQ_bw[3] = 400e3;
        }
        else if ( (strcmp(*argv, "-c") == 0) || (strcmp(*argv, "--cnt") == 0) ) {
            option_cont = 1;
        }
        else if ( (strcmp(*argv, "-s") == 0) || (strcmp(*argv, "--silent") == 0) ) {
            option_silent = 1;
        }
        else if ( (strcmp(*argv, "-t") == 0) || (strcmp(*argv, "--time") == 0) ) {
            ++argv;
            if (*argv) tl = atof(*argv);
            else return -50;
        }
        else if ( (strcmp(*argv, "-d2") == 0) ) {
            option_d2 = 1;
        }
        else if ( (strcmp(*argv, "--ch2") == 0) ) { wav_channel = 1; }
        else if ( (strcmp(*argv, "--ths") == 0) ) {
            ++argv;
            if (*argv) {
                thres = atof(*argv);
                for (j = 0; j < Nrs; j++) rs_hdr[j].thres = thres;
            }
            else return -50;
        }
        else if (strcmp(*argv, "-") == 0) {
            ++argv;
            if (*argv) sample_rate = atoi(*argv); else return -1;
            ++argv;
            if (*argv) bits_sample = atoi(*argv); else return -1;
            channels = 2;
            if (sample_rate < 1 || (bits_sample != 8 && bits_sample != 16 && bits_sample != 32)) {
                fprintf(stderr, "- <sr> <bs>\n");
                return -1;
            }
            option_pcmraw = 1;
        }
        else {
            fp = fopen(*argv, "rb");
            if (fp == NULL) {
                fprintf(stderr, "error: open %s\n", *argv);
                return -50;
            }
            wavloaded = 1;
        }
        ++argv;
    }
    if (!wavloaded) fp = stdin;

    if (option_d2) {
        option_cont = 0;
    }

    if (option_pcmraw == 0) {
        j = read_wav_header(fp, wav_channel);
        if ( j < 0 ) {
            fclose(fp);
            fprintf(stderr, "error: wav header\n");
            return -50;
        }
    }

    if (option_iq && channels < 2) {
        fprintf(stderr, "error: iq channels < 2\n");
        return -50;
    }

    K = init_buffers();
    if ( K < 0 ) {
        fprintf(stderr, "error: init buffers\n");
        return -50;
    };

    D = N_DFT/2 - 3;
    df = bin2freq(1);
    mf = 50.0/df;
    if (mf < 1) mf = 1;

    bin2200 = freq2bin(2200);
    bin2400 = freq2bin(2400);
    bin800 = freq2bin(800);
    bin1200 = freq2bin(1200);

    nD = 0;
    nT = 0;
    T = sample_rate / D;

    for (j = 0; j < Nrs; j++) {
        mv[j] = 0.0;
        mv_pos[j] = 0;
        mp[j] = 0;
    }
    j_max = 0;
    mv_max = 0.0;

    k = 0;

    while ( f32buf_sample(fp, option_inv) != EOF ) {

        if (tl > 0 && sample_in > (tl+1)*sample_rate) break;

        xn_afsk[nD % D] = buf_fm[rs_hdr[idxIMETafsk].lpIQ][sample_out % M];
        nD++;
        if (nD % D == 0) {
            dft(xn_afsk, X);
            for (m = 0; m < N_DFT; m++) db_afsk[m] += cabs(X[m]);
            nT += 1;
        }
        if (nT >= T) {

            pow2200 = 0.0;
            for (n = 0; n < mf; n++) pow2200 += db_afsk[ bin2200 - mf/4 + n ];

            pow2400 = 0.0;
            for (n = 0; n < mf; n++) pow2400 += db_afsk[ bin2400 - mf/4 + n ];

            pow800 = 0.0;
            for (n = 0; n < mf; n++) pow800 += db_afsk[ bin800 - mf/4 + n ];

            pow1200 = 0.0;
            for (n = 0; n < mf; n++) pow1200 += db_afsk[ bin1200 - mf/4 + n ];

            nT = 0;
            for (m = 0; m < N_DFT; m++) db_afsk[m] = 0;
        }


        k += 1;

        if (k >= K-4) {
            for (j = 0; j <= idxIMETafsk; j++) {

                if ( !type_enabled[j] ) continue;

                mv0_pos[j] = mv_pos[j];
                mp[j] = getCorrDFT(K, 0, mv+j, mv_pos+j, rs_hdr+j);
            }
            k = 0;
        }
        else {
            continue;
        }

        header_found = 0;
        for (j = 0; j <= idxIMETafsk; j++)
        {
            if ( !type_enabled[j] ) continue;
            if (mp[j] > 0 && (mv[j] > rs_hdr[j].thres || mv[j] < -rs_hdr[j].thres)) {
                if (mv_pos[j] > mv0_pos[j]) {

                    herrs = headcmp(1, mv_pos[j], mv[j]<0, rs_hdr+j);
                    if (herrs < rs_hdr[j].herrs)
                    {
                        if (strncmp(rs_hdr[j].type, "M10", 3) == 0 || strncmp(rs_hdr[j].type, "M20", 3) == 0)
                        {
                            ui32_t bytes = frm_M10(mv_pos[j], mv[j]<0, rs_hdr+j);
                            int len = (bytes >> 8) & 0xFF;
                            int h = hw(bytes & 0x0F);
                            if (h < 2 || h == 2 && (bytes&0xF0) == 0x20) {
                                rs_hdr[j].type = "M20";
                                rs_hdr[j].tn = tn_M20;
                            }
                            else {
                                rs_hdr[j].type = "M10";
                                rs_hdr[j].tn = tn_M10;
                            }
                            frm2_M10M20 = bytes;
                        }

                        if ( strncmp(rs_hdr[j].type, "IMETafsk", 8) == 0 )
                        {

                            mv[j] = fabs(mv[j]);

                            if (pow2200 > pow2400 && pow2400 > 0.0) {
                                if (pow2200 > pow800 && pow800 > 0.0) {
                                    int _j0 = j;
                                    if (option_iq && set_lpIQ > 50e3) j = idxRS; else j = idxI4;
                                    mv[j] = mv[_j0];
                                    mv_pos[j] = mv_pos[_j0];
                                    rs_hdr[j].dc = rs_hdr[_j0].dc;
                                    rs_hdr[j].df = rs_hdr[_j0].df;
                                    mv[_j0] = 0.0;
                                    header_found = 1;
                                }
                                else mv[j] = 0.0;
                            }
                            else {
                                mv[j] = 0.0;
                            }
                        }
                        else {
                            header_found = 1;
                        }

                        if (header_found) {
                            if (!option_silent && (mv[j] > rs_hdr[j].thres || mv[j] < -rs_hdr[j].thres)) {
                                if (option_d2) {
                                    rs_detect2[j] += 1;
                                    d2_tn = rs_d2();
                                    if ( d2_tn == Nrs ) header_found = 0;
                                }
                                if ( !option_d2 || j == d2_tn ) {
                                    if (option_verbose) fprintf(stdout, "sample: %d\n", mv_pos[j]);
                                    fprintf(stdout, "%s: %.4f", rs_hdr[j].type, mv[j]);
                                    if (strncmp(rs_hdr[j].type, "M10", 3) == 0 || strncmp(rs_hdr[j].type, "M20", 3) == 0)
                                    {
                                        if (option_verbose) fprintf(stdout, " [%04X]", frm2_M10M20 & 0xFFFF);
                                        frm2_M10M20 = 0;
                                    }
                                    if (option_dc && option_iq) {
                                        fprintf(stdout, " , %+.1fHz", rs_hdr[j].df*sr_base);
                                        if (option_verbose) {
                                            fprintf(stdout, "   [ fq-ofs: %+.6f", rs_hdr[j].df);
                                            fprintf(stdout, " = %+.1fHz ]", rs_hdr[j].df*sr_base);
                                        }
                                    }
                                    fprintf(stdout, "\n");
                                }
                            }

                            if ( fabs(mv_max) < fabs(mv[j]) ) {
                                mv_max = mv[j];
                                j_max = j;
                            }
                        }
                    }
                }
            }
        }

        if (header_found && !option_cont || d2_tn < Nrs) break;
        header_found = 0;
        for (j = 0; j < Nrs; j++) mv[j] = 0.0;
    }

ende:
    free_buffers();
    fclose(fp);

    if (mv_max) {
        if (mv_max < 0 && j_max < 3) header_found = -1;
        else header_found = 1;
    }
    else header_found = 0;

    return (header_found * rs_hdr[j_max].tn);
}
