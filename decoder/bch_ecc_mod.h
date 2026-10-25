

#ifdef INCLUDESTATIC
    #define INCSTAT static
#else
    #ifndef INTTYPES
    #define INTTYPES
    typedef unsigned char  ui8_t;
    typedef unsigned short ui16_t;
    typedef unsigned int   ui32_t;
    typedef char  i8_t;
    typedef short i16_t;
    typedef int   i32_t;
    #endif
    #define INCSTAT
#endif


#define MAX_DEG 254


typedef struct {
    ui32_t f;
    ui32_t ord;
    ui8_t alpha;
    ui8_t exp_a[256];
    ui8_t log_a[256];
} GF_t;

typedef struct {
    ui8_t N;
    ui8_t t;
    ui8_t R;
    ui8_t K;
    ui8_t b;
    ui8_t p; ui8_t ip;
    ui8_t g[MAX_DEG+1];
    GF_t GF;
} RS_t;


static GF_t GF256RS = { .f     = 0x11D,
                        .ord   = 256,
                        .alpha = 0x02,
                        .exp_a = {0},
                        .log_a = {0} };

static GF_t GF256RSccsds = { .f     = 0x187,
                             .ord   = 256,
                             .alpha = 0x02,
                             .exp_a = {0},
                             .log_a = {0} };

static GF_t GF64BCH = { .f     = 0x43,
                        .ord   = 64,
                        .alpha = 0x02,
                        .exp_a = {0},
                        .log_a = {0} };

static GF_t GF16RS = { .f     = 0x13,
                       .ord   = 16,
                       .alpha = 0x02,
                       .exp_a = {0},
                       .log_a = {0} };

static GF_t GF256AES = { .f     = 0x11B,
                         .ord   = 256,
                         .alpha = 0x03,
                         .exp_a = {0},
                         .log_a = {0} };


static RS_t RS256 = { 255, 12, 24, 231, 0, 1, 1, {0}, {0} };
static RS_t RS256ccsds = { 255, 16, 32, 223, 112, 11, 116, {0}, {0} };
static RS_t BCH64 = {  63,  2, 12,  51, 1, 1, 1, {0}, {0} };

static RS_t RS16ccsds = { 15, 2, 4, 11, 6, 1, 1, {0}, {0} };


#ifndef INCLUDESTATIC

int rs_init_RS(RS_t *RS);
int rs_init_RS255(RS_t *RS);
int rs_init_RS255ccsds(RS_t *RS);
int rs_init_RS15ccsds(RS_t *RS);
int rs_init_BCH64(RS_t *RS);

int rs_encode(RS_t *RS, ui8_t cw[]);
int rs_decode(RS_t *RS, ui8_t cw[], ui8_t *err_pos, ui8_t *err_val);
int rs_decode_ErrEra(RS_t *RS, ui8_t cw[], int nera, ui8_t era_pos[], ui8_t *err_pos, ui8_t *err_val);
int rs_decode_bch_gf2t2(RS_t *RS, ui8_t cw[], ui8_t *err_pos, ui8_t *err_val);

#endif

