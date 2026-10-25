


#define  PI  (3.1415926535897932384626433832795)

#define  RELATIVISTIC_CLOCK_CORRECTION   (-4.442807633e-10)
#define  GRAVITY_CONSTANT                (3.986005e14)
#define  EARTH_ROTATION_RATE             (7.2921151467e-05)
#define  SECONDS_IN_WEEK                 (604800.0)
#define  LIGHTSPEED                      (299792458.0)

#define  RANGE_ESTIMATE                  (0.072)
#define  RANGERATE_ESTIMATE              (0.000)

#define EARTH_a      (6378137.0)
#define EARTH_b      (6356752.31424518)
#define EARTH_a2_b2  (EARTH_a*EARTH_a - EARTH_b*EARTH_b)



void ecef2elli(double X, double Y, double Z, double *lat, double *lon, double *alt) {
    double ea2 = EARTH_a2_b2 / (EARTH_a*EARTH_a),
           eb2 = EARTH_a2_b2 / (EARTH_b*EARTH_b);
    double phi, lam, R, p, t;
    double sint, cost;

    lam = atan2( Y , X );

    p = sqrt( X*X + Y*Y );
    t = atan2( Z*EARTH_a , p*EARTH_b );

    sint = sin(t);
    cost = cos(t);

    phi = atan2( Z + eb2 * EARTH_b * sint*sint*sint ,
                 p - ea2 * EARTH_a * cost*cost*cost );

    R = EARTH_a / sqrt( 1 - ea2*sin(phi)*sin(phi) );
    *alt = p / cos(phi) - R;

    *lat = phi*180.0/PI;
    *lon = lam*180.0/PI;
}


double dist(double X1, double Y1, double Z1, double X2, double Y2, double Z2) {
    return sqrt( (X2-X1)*(X2-X1) + (Y2-Y1)*(Y2-Y1) + (Z2-Z1)*(Z2-Z1) );
}


void rotZ(double x1, double y1, double z1, double angle, double *x2, double *y2, double *z2) {
    double cosa = cos(angle);
    double sina = sin(angle);
    *x2 =  cosa * x1 + sina * y1;
    *y2 = -sina * x1 + cosa * y1;
    *z2 = z1;
}




typedef struct {
    ui16_t prn;
    ui16_t week;
    ui32_t toa;
    char   epoch[20];
    double toe;
    double toc;
    double e;
    double delta_n;
    double delta_i;
    double i0;
    double OmegaDot;
    double sqrta;
    double Omega0;
    double w;
    double M0;
    double tgd;
    double idot;
    double cuc;
    double cus;
    double crc;
    double crs;
    double cic;
    double cis;
    double af0;
    double af1;
    double af2;
    int gpsweek;
    ui16_t svn;
    ui8_t  ura;
    ui8_t  health;
    ui8_t  conf;
} EPHEM_t;

typedef struct {
    ui32_t t;
    double pseudorange;
    double pseudorate;
    double clock_corr;
    double clock_drift;
    double X;
    double Y;
    double Z;
    double vX;
    double vY;
    double vZ;
    int ephhr;
    double PR;
    double ephtime;
    int prn;
} SAT_t;

typedef struct {double X; double Y; double Z;} LOC_t;

typedef struct {double  X;  double Y;  double Z;
                double vX; double vY; double vZ;} VEL_t;




int read_SEMalmanac(FILE *fp, EPHEM_t *alm) {
    int l, j;
    char buf[64];
    unsigned n, week, toa, ui;
    double dbl;

    l = fscanf(fp, "%u", &n);    if (l != 1) return -1;
    l = fscanf(fp, "%s", buf);   if (l != 1) return -1;
    l = fscanf(fp, "%u", &week); if (l != 1) return -1;
    l = fscanf(fp, "%u", &toa);  if (l != 1) return -1;

    for (j = 1; j <= n; j++) {

        alm[j].week = (ui16_t)week;
        alm[j].toa  = (ui32_t)toa;
        alm[j].toe  = (double)toa;
        alm[j].toc  = alm[j].toe;

        l = fscanf(fp, "%u", &ui);    if (l != 1) return -1;  alm[j].prn = (ui16_t)ui;
        l = fscanf(fp, "%u", &ui);    if (l != 1) return -2;  alm[j].svn = (ui16_t)ui;
        l = fscanf(fp, "%u", &ui);    if (l != 1) return -3;  alm[j].ura = (ui8_t)ui;
        l = fscanf(fp, "%lf", &dbl);  if (l != 1) return -4;  alm[j].e = dbl;
        l = fscanf(fp, "%lf", &dbl);  if (l != 1) return -5;  alm[j].delta_i = dbl;
                                                              alm[j].i0 = (0.30 + alm[j].delta_i) * PI;
        l = fscanf(fp, "%lf", &dbl);  if (l != 1) return -6;  alm[j].OmegaDot = dbl * PI;
        l = fscanf(fp, "%lf", &dbl);  if (l != 1) return -7;  alm[j].sqrta = dbl;
        l = fscanf(fp, "%lf", &dbl);  if (l != 1) return -6;  alm[j].Omega0 = dbl * PI;
        l = fscanf(fp, "%lf", &dbl);  if (l != 1) return -8;  alm[j].w = dbl * PI;
        l = fscanf(fp, "%lf", &dbl);  if (l != 1) return -9;  alm[j].M0 = dbl * PI;
        l = fscanf(fp, "%lf", &dbl);  if (l != 1) return -10; alm[j].af0 = dbl;
        l = fscanf(fp, "%lf", &dbl);  if (l != 1) return -11; alm[j].af1 = dbl;
                                                              alm[j].af2 = 0;
                                                              alm[j].crc = 0;
                                                              alm[j].crs = 0;
                                                              alm[j].cuc = 0;
                                                              alm[j].cus = 0;
                                                              alm[j].cic = 0;
                                                              alm[j].cis = 0;
                                                              alm[j].tgd = 0;
                                                              alm[j].idot = 0;
                                                              alm[j].delta_n = 0;
        l = fscanf(fp, "%u", &ui);    if (l != 1) return -12; alm[j].health = (ui8_t)ui;
        l = fscanf(fp, "%u", &ui);    if (l != 1) return -13; alm[j].conf = (ui8_t)ui;
    }

    return 0;
}

int read_RNXephemeris(FILE *fp, EPHEM_t eph[][24]) {
    int l, i;
    char buf[64], str[20];
    char buf_header[83];
    char *pbuf;
    unsigned ui;
    double dbl;
    int c;
    EPHEM_t ephem = {};
    int hr = 0;

    do {
        pbuf = fgets(buf_header, 82, fp);
        buf_header[82] = '\0';
    } while ( pbuf  &&  !strstr(buf_header, "END OF HEADER") );



    while (hr < 24) {


        l = fread(buf,  3, 1, fp);    if (l != 1) break;  buf[ 3] = 0;  sscanf(buf, "%d", &ui);
        ephem.prn = ui;


        for (i = 0; i < 16; i++) ephem.epoch[i] = '0';
        ephem.epoch[16] = '\0';

        l = fread(buf, 19, 1, fp);    if (l != 1) break;  buf[19] = 0;

        for (i = 0; i < 6; i++) {
            c = buf[3*i  ]; if (c == ' ') c = '0'; str[2*i  ] = c;
            c = buf[3*i+1]; if (c == ' ') c = '0'; str[2*i+1] = c;
        }
        str[12] = buf[17];
        str[13] = buf[18];
        str[14] = '\0';

        strncpy(ephem.epoch  , "20", 2);
        strncpy(ephem.epoch+2, str, 15);
        ephem.epoch[16] = '\0';

        strncpy(str, buf+9, 2); str[2] = '\0';
        hr  = atoi(str);

        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.af0 = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.af1 = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.af2 = dbl;
        if ((c=fgetc(fp)) == EOF) break;

        l = fread(buf,  3, 1, fp);    if (l != 1) break;  buf[ 3] = 0;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl);
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.crs = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.delta_n = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.M0 = dbl;
        if ((c=fgetc(fp)) == EOF) break;

        l = fread(buf,  3, 1, fp);    if (l != 1) break;  buf[ 3] = 0;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.cuc = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.e = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.cus = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.sqrta = dbl;
        if ((c=fgetc(fp)) == EOF) break;

        l = fread(buf,  3, 1, fp);    if (l != 1) break;  buf[ 3] = 0;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.toe = dbl;
                                                                                                                                    ephem.toc = ephem.toe;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.cic = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.Omega0 = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.cis = dbl;
        if ((c=fgetc(fp)) == EOF) break;

        l = fread(buf,  3, 1, fp);    if (l != 1) break;  buf[ 3] = 0;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.i0 = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.crc = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.w = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.OmegaDot = dbl;
        if ((c=fgetc(fp)) == EOF) break;

        l = fread(buf,  3, 1, fp);    if (l != 1) break;  buf[ 3] = 0;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.idot = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl);
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.gpsweek = (int)dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl);
        if ((c=fgetc(fp)) == EOF) break;

        l = fread(buf,  3, 1, fp);    if (l != 1) break;  buf[ 3] = 0;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl);
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.health = (ui8_t)(dbl+0.1);
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.tgd = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl);
        if ((c=fgetc(fp)) == EOF) break;

        l = fread(buf,  3, 1, fp);    if (l != 1) break;  buf[ 3] = 0;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl);
        pbuf = fgets(buf_header, 82, fp);


        ephem.week = 1;
        eph[ephem.prn][hr] = ephem;

        if (pbuf == NULL) break;
    }

    return 0;
}

EPHEM_t *read_RNXpephs(FILE *fp) {
    int l, i, n;
    char buffer[86];
    char buf[64], str[20];
    char *pbuf;
    unsigned ui;
    double dbl;
    int c;
    EPHEM_t ephem = {}, *te = NULL;
    int count = 0;
    long fpos;

    do {
        pbuf = fgets(buffer, 84, fp);
        buffer[82] = '\0';
    } while ( pbuf  &&  !strstr(buffer, "END OF HEADER") );

    if (pbuf == NULL) return NULL;
    fpos = ftell(fp);

    count = 0;
    while (count >= 0) {
        pbuf = fgets(buffer, 84, fp); if (pbuf == 0) break;
        strncpy(str, buffer, 3);
        str[3] = '\0';
        sscanf(str, "%d", &ui);
        if (ui < 33) count++;
        for (i = 1; i < 8; i++) {
            pbuf = fgets(buffer, 84, fp); if (pbuf == 0) break;
        }
    }

    fseek(fp, fpos, SEEK_SET);

    te = calloc( count+1, sizeof(ephem) );
    if (te == NULL) return NULL;

    n = 0;

    while (count > 0) {


        l = fread(buf,  3, 1, fp);    if (l != 1) break;  buf[ 3] = 0;  sscanf(buf, "%d", &ui);
        ephem.prn = ui;

        for (i = 0; i < 16; i++) ephem.epoch[i] = '0';
        ephem.epoch[16] = '\0';

        l = fread(buf, 19, 1, fp);    if (l != 1) break;  buf[19] = 0;

        for (i = 0; i < 6; i++) {
            c = buf[3*i  ]; if (c == ' ') c = '0'; str[2*i  ] = c;
            c = buf[3*i+1]; if (c == ' ') c = '0'; str[2*i+1] = c;
        }
        str[12] = buf[17];
        str[13] = buf[18];
        str[14] = '\0';

        strncpy(ephem.epoch  , "20", 2);
        strncpy(ephem.epoch+2, str, 15);
        ephem.epoch[16] = '\0';

        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.af0 = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.af1 = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.af2 = dbl;
        while ((c=fgetc(fp)) != '\n') { if (c == EOF) break; }

        l = fread(buf,  3, 1, fp);    if (l != 1) break;  buf[ 3] = 0;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl);
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.crs = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.delta_n = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.M0 = dbl;
        while ((c=fgetc(fp)) != '\n') { if (c == EOF) break; }

        l = fread(buf,  3, 1, fp);    if (l != 1) break;  buf[ 3] = 0;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.cuc = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.e = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.cus = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.sqrta = dbl;
        while ((c=fgetc(fp)) != '\n') { if (c == EOF) break; }

        l = fread(buf,  3, 1, fp);    if (l != 1) break;  buf[ 3] = 0;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.toe = dbl;
                                                                                                                                    ephem.toc = ephem.toe;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.cic = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.Omega0 = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.cis = dbl;
        while ((c=fgetc(fp)) != '\n') { if (c == EOF) break; }

        l = fread(buf,  3, 1, fp);    if (l != 1) break;  buf[ 3] = 0;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.i0 = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.crc = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.w = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.OmegaDot = dbl;
        while ((c=fgetc(fp)) != '\n') { if (c == EOF) break; }

        l = fread(buf,  3, 1, fp);    if (l != 1) break;  buf[ 3] = 0;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.idot = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl);
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.gpsweek = (int)dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl);
        while ((c=fgetc(fp)) != '\n') { if (c == EOF) break; }

        l = fread(buf,  3, 1, fp);    if (l != 1) break;  buf[ 3] = 0;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl);
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.health = (ui8_t)(dbl+0.1);
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl); ephem.tgd = dbl;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl);
        while ((c=fgetc(fp)) != '\n') { if (c == EOF) break; }

        l = fread(buf,  3, 1, fp);    if (l != 1) break;  buf[ 3] = 0;
        l = fread(buf, 19, 1, fp);    if (l != 1) break;  if (buf[15] == 'D') buf[15] = 'E'; buf[19] = 0; sscanf(buf, "%lf", &dbl);
        pbuf = fgets(buffer, 84, fp);

        ephem.week = 1;

        te[n] = ephem;
        n += 1;


        if (pbuf == NULL) break;
    }

    te[n].prn = 0;


    return te;
}



void GPS_SatelliteClockCorrection(
  const unsigned short transmission_gpsweek,
  const double         transmission_gpstow,
  const unsigned short ephem_week,
  const double         toe,
  const double         toc,
  const double         af0,
  const double         af1,
  const double         af2,
  const double         ecc,
  const double         sqrta,
  const double         delta_n,
  const double         m0,
  const double         tgd,
  double*  clock_correction )
{
    int j;

    double tot;
    double tk;
    double tc;
    double d_tr;
    double d_tsv;
    double a;
    double n;
    double M;
    double E;

    tot = transmission_gpsweek*SECONDS_IN_WEEK + transmission_gpstow;
    tk  = tot - (ephem_week*SECONDS_IN_WEEK + toe);
    tc  = tot - (ephem_week*SECONDS_IN_WEEK + toc);

    a = sqrta*sqrta;
    n = sqrt( GRAVITY_CONSTANT / (a*a*a) );
    n += delta_n;

    M = m0 + n*tk;
    E = M;
    for( j = 0; j < 7; j++ ) {
        E = M + ecc * sin(E);
    }

    d_tr = RELATIVISTIC_CLOCK_CORRECTION * ecc * sqrta * sin(E);
    d_tr *= LIGHTSPEED;

    d_tsv = af0 + af1*tc + af2*tc*tc;

    d_tsv -= tgd;

    *clock_correction = d_tsv*LIGHTSPEED + d_tr;

}

void GPS_ComputeSatellitePosition(
  const unsigned short transmission_gpsweek,
  const double         transmission_gpstow,
  const unsigned short ephem_week,
  const double         toe,
  const double         m0,
  const double         delta_n,
  const double         ecc,
  const double         sqrta,
  const double         omega0,
  const double         i0,
  const double         w,
  const double         omegadot,
  const double         idot,
  const double         cuc,
  const double         cus,
  const double         crc,
  const double         crs,
  const double         cic,
  const double         cis,
  double* x,
  double* y,
  double* z )
{
    int j;

    double tot;
    double tk;
    double a;
    double n;
    double M;
    double E;
    double v;
    double u;
    double r;
    double i;
    double cos2u;
    double sin2u;
    double d_u;
    double d_r;
    double d_i;
    double x_op;
    double y_op;
    double omegak;
    double cos_omegak;
    double sin_omegak;
    double cosu;
    double sinu;
    double cosi;
    double sini;
    double cosE;
    double sinE;


    tot = transmission_gpsweek*SECONDS_IN_WEEK + transmission_gpstow;
    tk  = tot - (ephem_week*SECONDS_IN_WEEK + toe);

    a = sqrta*sqrta;
    n = sqrt( GRAVITY_CONSTANT / (a*a*a) );
    n += delta_n;

    M = m0 + n*tk;
    E = M;
    for( j = 0; j < 7; j++ ) {
        E = M + ecc * sin(E);
    }

    cosE = cos(E);
    sinE = sin(E);

    v = atan2( (sqrt(1.0 - ecc*ecc)*sinE),  (cosE - ecc) );
    u = v + w;
    r = a * (1.0 - ecc * cos(E));
    i = i0;

    cos2u = cos(2.0*u);
    sin2u = sin(2.0*u);
    d_u = cuc * cos2u  +  cus * sin2u;
    d_r = crc * cos2u  +  crs * sin2u;
    d_i = cic * cos2u  +  cis * sin2u;

    u += d_u;
    r += d_r;
    i += d_i + idot * tk;

    cosu = cos(u);
    sinu = sin(u);
    x_op = r * cosu;
    y_op = r * sinu;


    omegak = omega0 + omegadot*tk - EARTH_ROTATION_RATE*(tk + toe);

    cos_omegak = cos(omegak);
    sin_omegak = sin(omegak);
    cosi = cos(i);
    sini = sin(i);

    *x = x_op * cos_omegak - y_op * sin_omegak * cosi;
    *y = x_op * sin_omegak + y_op * cos_omegak * cosi;
    *z = y_op * sini;

}

void GPS_SatellitePosition_Ephem(
  const unsigned short gpsweek,
  const double         gpstow,
  EPHEM_t ephem,
  double* clock_correction,
  double* satX,
  double* satY,
  double* satZ
  )
{
    double tow;
    double x;
    double y;
    double z;
    unsigned short week;


    x = y = z = 0.0;

    GPS_SatelliteClockCorrection( gpsweek, gpstow,
        ephem.week, ephem.toe, ephem.toc, ephem.af0,
        ephem.af1, ephem.af2, ephem.e, ephem.sqrta,
        ephem.delta_n, ephem.M0, ephem.tgd, clock_correction );


    week = gpsweek;
    tow = gpstow + (*clock_correction)/LIGHTSPEED;
    if ( tow < 0.0 ) {
        tow += SECONDS_IN_WEEK;
        week--;
    }
    if ( tow > SECONDS_IN_WEEK ) {
        tow -= SECONDS_IN_WEEK;
        week++;
    }

    GPS_ComputeSatellitePosition( week, tow,
        ephem.week, ephem.toe, ephem.M0, ephem.delta_n, ephem.e, ephem.sqrta,
        ephem.Omega0, ephem.i0, ephem.w, ephem.OmegaDot, ephem.idot,
        ephem.cuc, ephem.cus, ephem.crc, ephem.crs, ephem.cic, ephem.cis,
        &x, &y, &z );

    *satX = x;
    *satY = y;
    *satZ = z;

}



int NAV_ClosedFormSolution_FromPseudorange(
  SAT_t sats[4],
  double* latitude,
  double* longitude,
  double* height,
  double* rx_clock_bias,
  double pos_ecef[3] )
{

    double p1 = sats[0].pseudorange + sats[0].clock_corr;
    double p2 = sats[1].pseudorange + sats[1].clock_corr;
    double p3 = sats[2].pseudorange + sats[2].clock_corr;
    double p4 = sats[3].pseudorange + sats[3].clock_corr;

    double x1, y1, z1;
    double x2, y2, z2;
    double x3, y3, z3;
    double x4, y4, z4;

    rotZ(sats[0].X, sats[0].Y, sats[0].Z, EARTH_ROTATION_RATE*RANGE_ESTIMATE, &x1, &y1, &z1);
    rotZ(sats[1].X, sats[1].Y, sats[1].Z, EARTH_ROTATION_RATE*RANGE_ESTIMATE, &x2, &y2, &z2);
    rotZ(sats[2].X, sats[2].Y, sats[2].Z, EARTH_ROTATION_RATE*RANGE_ESTIMATE, &x3, &y3, &z3);
    rotZ(sats[3].X, sats[3].Y, sats[3].Z, EARTH_ROTATION_RATE*RANGE_ESTIMATE, &x4, &y4, &z4);


    double x12, x13, x14;
    double y12, y13, y14;
    double z12, z13, z14;
    double p21, p31, p41;

    double k1, k2, k3;
    double c1, c2, c3;
    double f1, f2, f3;
    double m1, m2, m3;

    double d1;
    double d2;
    double detA;
    double tmp1;
    double tmp2;
    double tmp3;

    double A[3][3];
    double D[3][3];

    typedef struct {
        double x;
        double y;
        double z;
    } struct_SOLN;

    struct_SOLN s1;
    struct_SOLN s2;

    struct_SOLN stmp;
    double dtmp;
    double x0, y0, z0;
    double latS, lonS, altS,
           lat1, lon1, alt1,
           lat2, lon2, alt2;
    double d2_1, d2_2;


    x12 = x1 - x2;
    x13 = x1 - x3;
    x14 = x1 - x4;

    y12 = y1 - y2;
    y13 = y1 - y3;
    y14 = y1 - y4;

    z12 = z1 - z2;
    z13 = z1 - z3;
    z14 = z1 - z4;

    p21 = p2 - p1;
    p31 = p3 - p1;
    p41 = p4 - p1;

    k1 = x12*x12 + y12*y12 + z12*z12 - p21*p21;
    k2 = x13*x13 + y13*y13 + z13*z13 - p31*p31;
    k3 = x14*x14 + y14*y14 + z14*z14 - p41*p41;

    A[0][0] = 2.0*x12;
    A[1][0] = 2.0*x13;
    A[2][0] = 2.0*x14;

    A[0][1] = 2.0*y12;
    A[1][1] = 2.0*y13;
    A[2][1] = 2.0*y14;

    A[0][2] = 2.0*z12;
    A[1][2] = 2.0*z13;
    A[2][2] = 2.0*z14;

    tmp1 = A[1][1]*A[2][2] - A[2][1]*A[1][2];
    tmp2 = A[1][0]*A[2][2] - A[2][0]*A[1][2];
    tmp3 = A[1][0]*A[2][1] - A[2][0]*A[1][1];

    detA = A[0][0]*tmp1 - A[0][1]*tmp2 + A[0][2]*tmp3;

    D[0][0] =  tmp1;
    D[1][0] = -tmp2;
    D[2][0] =  tmp3;

    D[0][1] = -A[0][1]*A[2][2] + A[2][1]*A[0][2];
    D[1][1] =  A[0][0]*A[2][2] - A[2][0]*A[0][2];
    D[2][1] = -A[0][0]*A[2][1] + A[2][0]*A[0][1];

    D[0][2] =  A[0][1]*A[1][2] - A[1][1]*A[0][2];
    D[1][2] = -A[0][0]*A[1][2] + A[1][0]*A[0][2];
    D[2][2] =  A[0][0]*A[1][1] - A[1][0]*A[0][1];

    c1 = (D[0][0]*p21 + D[0][1]*p31 + D[0][2]*p41) * 2.0 / detA;
    c2 = (D[1][0]*p21 + D[1][1]*p31 + D[1][2]*p41) * 2.0 / detA;
    c3 = (D[2][0]*p21 + D[2][1]*p31 + D[2][2]*p41) * 2.0 / detA;

    f1 = (D[0][0]*k1 + D[0][1]*k2 + D[0][2]*k3) / detA;
    f2 = (D[1][0]*k1 + D[1][1]*k2 + D[1][2]*k3) / detA;
    f3 = (D[2][0]*k1 + D[2][1]*k2 + D[2][2]*k3) / detA;

    m1 = c1*c1 + c2*c2 + c3*c3 - 1.0;
    m2 = -2.0*( c1*f1 + c2*f2 + c3*f3 );
    m3 = f1*f1 + f2*f2 + f3*f3;

    tmp1 = m2*m2 - 4.0*m1*m3;
    if ( tmp1 < 0 ) {
        return -1;
    }

    d1 = ( -m2 - sqrt( tmp1 ) ) * 0.5 / m1;
    d2 = ( -m2 + sqrt( tmp1 ) ) * 0.5 / m1;

    s1.x = c1*d1 - f1 + x1;
    s1.y = c2*d1 - f2 + y1;
    s1.z = c3*d1 - f3 + z1;

    s2.x = c1*d2 - f1 + x1;
    s2.y = c2*d2 - f2 + y1;
    s2.z = c3*d2 - f3 + z1;

    tmp1 = sqrt( s1.x*s1.x + s1.y*s1.y + s1.z*s1.z );
    tmp2 = sqrt( s2.x*s2.x + s2.y*s2.y + s2.z*s2.z );

    tmp1 = fabs( tmp1 - 6371000.0 );
    tmp2 = fabs( tmp2 - 6371000.0 );

    if ( tmp2 < tmp1  &&  tmp1 >= 60000 ) {
        stmp = s1; s1 = s2; s2 = stmp;
        dtmp = d1; d1 = d2; d2 = dtmp;
    }
    else if ( tmp2 < 60000 ) {
        x0 = (x1+x2+x3+x4)/4.0;
        y0 = (y1+y2+y3+y4)/4.0;
        z0 = (z1+z2+z3+z4)/4.0;
        ecef2elli( x0, y0, z0, &latS, &lonS, &altS );

        ecef2elli( s1.x, s1.y, s1.z, &lat1, &lon1, &alt1 );
        ecef2elli( s2.x, s2.y, s2.z, &lat2, &lon2, &alt2 );

        d2_1 = sqrt( (latS-lat1)*(latS-lat1) + (lonS-lon1)*(lonS-lon1) );
        d2_2 = sqrt( (latS-lat2)*(latS-lat2) + (lonS-lon2)*(lonS-lon2) );

        if ( d2_2 < d2_1 ) {
            stmp = s1; s1 = s2; s2 = stmp;
            dtmp = d1; d1 = d2; d2 = dtmp;
        }
    }

    ecef2elli( s1.x, s1.y, s1.z, &lat1, &lon1, &alt1 );

    *latitude  = lat1;
    *longitude = lon1;
    *height    = alt1;

    *rx_clock_bias = d1;

    pos_ecef[0] = s1.x;
    pos_ecef[1] = s1.y;
    pos_ecef[2] = s1.z;

    if( *height < -1500.0 || *height > 50000.0 ) {
        return -2;
    }

    return 0;
}




int trace_invert(double mat[4][4], double trinv[4])
{
    double Det2_12_01 = mat[1][0] * mat[2][1] - mat[1][1] * mat[2][0];
    double Det2_12_02 = mat[1][0] * mat[2][2] - mat[1][2] * mat[2][0];
    double Det2_12_12 = mat[1][1] * mat[2][2] - mat[1][2] * mat[2][1];
    double Det2_13_01 = mat[1][0] * mat[3][1] - mat[1][1] * mat[3][0];
    double Det2_13_03 = mat[1][0] * mat[3][3] - mat[1][3] * mat[3][0];
    double Det2_13_13 = mat[1][1] * mat[3][3] - mat[1][3] * mat[3][1];
    double Det2_23_01 = mat[2][0] * mat[3][1] - mat[2][1] * mat[3][0];
    double Det2_23_02 = mat[2][0] * mat[3][2] - mat[2][2] * mat[3][0];
    double Det2_23_03 = mat[2][0] * mat[3][3] - mat[2][3] * mat[3][0];
    double Det2_23_12 = mat[2][1] * mat[3][2] - mat[2][2] * mat[3][1];
    double Det2_23_13 = mat[2][1] * mat[3][3] - mat[2][3] * mat[3][1];
    double Det2_23_23 = mat[2][2] * mat[3][3] - mat[2][3] * mat[3][2];

    double Det3_012_012 = mat[0][0] * Det2_12_12 - mat[0][1] * Det2_12_02 + mat[0][2] * Det2_12_01;
    double Det3_013_013 = mat[0][0] * Det2_13_13 - mat[0][1] * Det2_13_03 + mat[0][3] * Det2_13_01;
    double Det3_023_023 = mat[0][0] * Det2_23_23 - mat[0][2] * Det2_23_03 + mat[0][3] * Det2_23_02;
    double Det3_123_012 = mat[1][0] * Det2_23_12 - mat[1][1] * Det2_23_02 + mat[1][2] * Det2_23_01;
    double Det3_123_013 = mat[1][0] * Det2_23_13 - mat[1][1] * Det2_23_03 + mat[1][3] * Det2_23_01;
    double Det3_123_023 = mat[1][0] * Det2_23_23 - mat[1][2] * Det2_23_03 + mat[1][3] * Det2_23_02;
    double Det3_123_123 = mat[1][1] * Det2_23_23 - mat[1][2] * Det2_23_13 + mat[1][3] * Det2_23_12;

    static double det;
    det = mat[0][0] * Det3_123_123
	    - mat[0][1] * Det3_123_023
	    + mat[0][2] * Det3_123_013
	    - mat[0][3] * Det3_123_012;

    if (fabs(det) < 0.0001)
	return -1;





    trinv[0] = Det3_123_123 / det;

    trinv[1] = Det3_023_023 / det;
    trinv[2] = Det3_013_013 / det;
    trinv[3] = Det3_012_012 / det;

    return 0;
}

int calc_DOPn(int n, SAT_t satss[], double pos_ecef[3], double DOP[4]) {
    int i, j, k;
    double norm[n], satpos[n][3];
    double SATS[n][4], AtA[4][4];

    for (i = 0; i < n; i++) {
        satpos[i][0] = satss[i].X-pos_ecef[0];
        satpos[i][1] = satss[i].Y-pos_ecef[1];
        satpos[i][2] = satss[i].Z-pos_ecef[2];
    }


    for (i = 0; i < n; i++) {
        norm[i] = sqrt( satpos[i][0]*satpos[i][0] + satpos[i][1]*satpos[i][1] + satpos[i][2]*satpos[i][2] );
        for (j = 0; j < 3; j++) {
            SATS[i][j] = satpos[i][j] / norm[i];
        }
        SATS[i][3] = 1;
    }

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            AtA[i][j] = 0.0;
            for (k = 0; k < n; k++) {
                AtA[i][j] += SATS[k][i]*SATS[k][j];
            }
        }
    }

    return trace_invert(AtA, DOP);
}


int rotE(SAT_t sat, double *x, double *y, double *z) {
    double range = sat.PR/LIGHTSPEED;
    if (range < 19e6  ||  range > 30e6) range = 21e6;
    rotZ(sat.X, sat.Y, sat.Z, EARTH_ROTATION_RATE*(range/LIGHTSPEED), x, y, z);
    return 0;
}


double lorentz(double a[4], double b[4]) {
    return a[0]*b[0] + a[1]*b[1] +a[2]*b[2] - a[3]*b[3];
}

int matrix_invert(double mat[4][4], double inv[4][4])
{
    double Det2_12_01 = mat[1][0] * mat[2][1] - mat[1][1] * mat[2][0];
    double Det2_12_02 = mat[1][0] * mat[2][2] - mat[1][2] * mat[2][0];
    double Det2_12_03 = mat[1][0] * mat[2][3] - mat[1][3] * mat[2][0];
    double Det2_12_12 = mat[1][1] * mat[2][2] - mat[1][2] * mat[2][1];
    double Det2_12_13 = mat[1][1] * mat[2][3] - mat[1][3] * mat[2][1];
    double Det2_12_23 = mat[1][2] * mat[2][3] - mat[1][3] * mat[2][2];
    double Det2_13_01 = mat[1][0] * mat[3][1] - mat[1][1] * mat[3][0];
    double Det2_13_02 = mat[1][0] * mat[3][2] - mat[1][2] * mat[3][0];
    double Det2_13_03 = mat[1][0] * mat[3][3] - mat[1][3] * mat[3][0];
    double Det2_13_12 = mat[1][1] * mat[3][2] - mat[1][2] * mat[3][1];
    double Det2_13_13 = mat[1][1] * mat[3][3] - mat[1][3] * mat[3][1];
    double Det2_13_23 = mat[1][2] * mat[3][3] - mat[1][3] * mat[3][2];
    double Det2_23_01 = mat[2][0] * mat[3][1] - mat[2][1] * mat[3][0];
    double Det2_23_02 = mat[2][0] * mat[3][2] - mat[2][2] * mat[3][0];
    double Det2_23_03 = mat[2][0] * mat[3][3] - mat[2][3] * mat[3][0];
    double Det2_23_12 = mat[2][1] * mat[3][2] - mat[2][2] * mat[3][1];
    double Det2_23_13 = mat[2][1] * mat[3][3] - mat[2][3] * mat[3][1];
    double Det2_23_23 = mat[2][2] * mat[3][3] - mat[2][3] * mat[3][2];

    double Det3_012_012 = mat[0][0] * Det2_12_12 - mat[0][1] * Det2_12_02 + mat[0][2] * Det2_12_01;
    double Det3_012_013 = mat[0][0] * Det2_12_13 - mat[0][1] * Det2_12_03 + mat[0][3] * Det2_12_01;
    double Det3_012_023 = mat[0][0] * Det2_12_23 - mat[0][2] * Det2_12_03 + mat[0][3] * Det2_12_02;
    double Det3_012_123 = mat[0][1] * Det2_12_23 - mat[0][2] * Det2_12_13 + mat[0][3] * Det2_12_12;
    double Det3_013_012 = mat[0][0] * Det2_13_12 - mat[0][1] * Det2_13_02 + mat[0][2] * Det2_13_01;
    double Det3_013_013 = mat[0][0] * Det2_13_13 - mat[0][1] * Det2_13_03 + mat[0][3] * Det2_13_01;
    double Det3_013_023 = mat[0][0] * Det2_13_23 - mat[0][2] * Det2_13_03 + mat[0][3] * Det2_13_02;
    double Det3_013_123 = mat[0][1] * Det2_13_23 - mat[0][2] * Det2_13_13 + mat[0][3] * Det2_13_12;
    double Det3_023_012 = mat[0][0] * Det2_23_12 - mat[0][1] * Det2_23_02 + mat[0][2] * Det2_23_01;
    double Det3_023_013 = mat[0][0] * Det2_23_13 - mat[0][1] * Det2_23_03 + mat[0][3] * Det2_23_01;
    double Det3_023_023 = mat[0][0] * Det2_23_23 - mat[0][2] * Det2_23_03 + mat[0][3] * Det2_23_02;
    double Det3_023_123 = mat[0][1] * Det2_23_23 - mat[0][2] * Det2_23_13 + mat[0][3] * Det2_23_12;
    double Det3_123_012 = mat[1][0] * Det2_23_12 - mat[1][1] * Det2_23_02 + mat[1][2] * Det2_23_01;
    double Det3_123_013 = mat[1][0] * Det2_23_13 - mat[1][1] * Det2_23_03 + mat[1][3] * Det2_23_01;
    double Det3_123_023 = mat[1][0] * Det2_23_23 - mat[1][2] * Det2_23_03 + mat[1][3] * Det2_23_02;
    double Det3_123_123 = mat[1][1] * Det2_23_23 - mat[1][2] * Det2_23_13 + mat[1][3] * Det2_23_12;

    static double det;
    det = mat[0][0] * Det3_123_123
	    - mat[0][1] * Det3_123_023
	    + mat[0][2] * Det3_123_013
	    - mat[0][3] * Det3_123_012;

    if (fabs(det) < 0.0001)
	return -1;

    inv[0][0] =  Det3_123_123 / det;
    inv[0][1] = -Det3_023_123 / det;
    inv[0][2] =  Det3_013_123 / det;
    inv[0][3] = -Det3_012_123 / det;

    inv[1][0] = -Det3_123_023 / det;
    inv[1][1] =  Det3_023_023 / det;
    inv[1][2] = -Det3_013_023 / det;
    inv[1][3] =  Det3_012_023 / det;

    inv[2][0] =  Det3_123_013 / det;
    inv[2][1] = -Det3_023_013 / det;
    inv[2][2] =  Det3_013_013 / det;
    inv[2][3] = -Det3_012_013 / det;

    inv[3][0] = -Det3_123_012 / det;
    inv[3][1] =  Det3_023_012 / det;
    inv[3][2] = -Det3_013_012 / det;
    inv[3][3] =  Det3_012_012 / det;

    return 0;
}

int NAV_bancroft1(int N, SAT_t sats[], double pos_ecef[3], double *cc) {

    int i, j, k;
    double B[N][4], BtB[4][4], BBinv[4][4], BBB[4][N];
    double a[N], Be[4], Ba[4];

    double q0, q1, q2, p, q, sq, x1, x2;
    double Lsg1[4], Lsg2[4];

    double tmp1, tmp2;
    double X, Y, Z;


    if (N < 4 || N > 12) return -1;

    for (i = 0; i < N; i++) {
        rotZ(sats[i].X, sats[i].Y, sats[i].Z, EARTH_ROTATION_RATE*RANGE_ESTIMATE, B[i], B[i]+1, B[i]+2);
        B[i][3] = sats[i].pseudorange + sats[i].clock_corr;
    }

    if (N == 4) {
        matrix_invert(B, BBB);
    }
    else {
        for (i = 0; i < 4; i++) {
            for (j = 0; j < 4; j++) {
                BtB[i][j] = 0.0;
                for (k = 0; k < N; k++) {
                    BtB[i][j] += B[k][i]*B[k][j];
                }
            }
        }
        matrix_invert(BtB, BBinv);
        for (i = 0; i < 4; i++) {
            for (j = 0; j < N; j++) {
                BBB[i][j] = 0.0;
                for (k = 0; k < 4; k++) {
                    BBB[i][j] += BBinv[i][k]*B[j][k];
                }
            }
        }
    }

    for (i = 0; i < 4; i++) {
        Be[i] = 0.0;
        for (k = 0; k < N; k++) {
            Be[i] += BBB[i][k]*1.0;
        }
    }

    for (i = 0; i < N; i++) {
        a[i] = 0.5 * lorentz(B[i], B[i]);
    }

    for (i = 0; i < 4; i++) {
        Ba[i] = 0.0;
        for (k = 0; k < N; k++) {
            Ba[i] += BBB[i][k]*a[k];
        }
    }

    q2 = lorentz(Be, Be);
    q1 = lorentz(Ba, Be)-1;
    q0 = lorentz(Ba, Ba);

    if (q2 == 0) return -2;

    p = q1/q2;
    q = q0/q2;

    sq = p*p - q;
    if (sq < 0) return -2;

    x1 = -p + sqrt(sq);
    x2 = -p - sqrt(sq);

    for (i = 0; i < 4; i++) {
        Lsg1[i] = x1*Be[i]+Ba[i];
        Lsg2[i] = x2*Be[i]+Ba[i];
    }
    Lsg1[3] = -Lsg1[3];
    Lsg2[3] = -Lsg2[3];


    tmp1 = sqrt( Lsg1[0]*Lsg1[0] + Lsg1[1]*Lsg1[1] + Lsg1[2]*Lsg1[2] );
    tmp2 = sqrt( Lsg2[0]*Lsg2[0] + Lsg2[1]*Lsg2[1] + Lsg2[2]*Lsg2[2] );

    tmp1 = fabs( tmp1 - 6371000.0 );
    tmp2 = fabs( tmp2 - 6371000.0 );

    if (tmp1 < tmp2) {
        X = Lsg1[0]; Y = Lsg1[1]; Z = Lsg1[2]; *cc = Lsg1[3];
    } else {
        X = Lsg2[0]; Y = Lsg2[1]; Z = Lsg2[2]; *cc = Lsg2[3];
    }
    pos_ecef[0] = X;
    pos_ecef[1] = Y;
    pos_ecef[2] = Z;

    return 0;
}

int NAV_bancroft2(int N, SAT_t sats[], double pos_ecef[3], double *cc) {

    int i, j, k;
    double B[N][4], BtB[4][4], BBinv[4][4], BBB[4][N];
    double a[N], Be[4], Ba[4];

    double q0, q1, q2, p, q, sq, x1, x2;
    double Lsg1[4], Lsg2[4];

    double tmp1, tmp2;
    double X, Y, Z;


    if (N < 4 || N > 12) return -1;

    for (i = 0; i < N; i++) {
        rotE(sats[i], B[i], B[i]+1, B[i]+2);
        B[i][3] = sats[i].PR;
    }

    if (N == 4) {
        matrix_invert(B, BBB);
    }
    else {
        for (i = 0; i < 4; i++) {
            for (j = 0; j < 4; j++) {
                BtB[i][j] = 0.0;
                for (k = 0; k < N; k++) {
                    BtB[i][j] += B[k][i]*B[k][j];
                }
            }
        }
        matrix_invert(BtB, BBinv);
        for (i = 0; i < 4; i++) {
            for (j = 0; j < N; j++) {
                BBB[i][j] = 0.0;
                for (k = 0; k < 4; k++) {
                    BBB[i][j] += BBinv[i][k]*B[j][k];
                }
            }
        }
    }

    for (i = 0; i < 4; i++) {
        Be[i] = 0.0;
        for (k = 0; k < N; k++) {
            Be[i] += BBB[i][k]*1.0;
        }
    }

    for (i = 0; i < N; i++) {
        a[i] = 0.5 * lorentz(B[i], B[i]);
    }

    for (i = 0; i < 4; i++) {
        Ba[i] = 0.0;
        for (k = 0; k < N; k++) {
            Ba[i] += BBB[i][k]*a[k];
        }
    }

    q2 = lorentz(Be, Be);
    q1 = lorentz(Ba, Be)-1;
    q0 = lorentz(Ba, Ba);

    if (q2 == 0) return -2;

    p = q1/q2;
    q = q0/q2;

    sq = p*p - q;
    if (sq < 0) return -2;

    x1 = -p + sqrt(sq);
    x2 = -p - sqrt(sq);

    for (i = 0; i < 4; i++) {
        Lsg1[i] = x1*Be[i]+Ba[i];
        Lsg2[i] = x2*Be[i]+Ba[i];
    }
    Lsg1[3] = -Lsg1[3];
    Lsg2[3] = -Lsg2[3];


    tmp1 = sqrt( Lsg1[0]*Lsg1[0] + Lsg1[1]*Lsg1[1] + Lsg1[2]*Lsg1[2] );
    tmp2 = sqrt( Lsg2[0]*Lsg2[0] + Lsg2[1]*Lsg2[1] + Lsg2[2]*Lsg2[2] );

    tmp1 = fabs( tmp1 - 6371000.0 );
    tmp2 = fabs( tmp2 - 6371000.0 );

    if (tmp1 < tmp2) {
        X = Lsg1[0]; Y = Lsg1[1]; Z = Lsg1[2]; *cc = Lsg1[3];
    } else {
        X = Lsg2[0]; Y = Lsg2[1]; Z = Lsg2[2]; *cc = Lsg2[3];
    }
    pos_ecef[0] = X;
    pos_ecef[1] = Y;
    pos_ecef[2] = Z;

    return 0;
}


int NAV_bancroft3(int N, SAT_t sats[], double pos_ecef1[3], double *cc1 , double pos_ecef2[3], double *cc2) {

    int i, j, k;
    double B[N][4], BtB[4][4], BBinv[4][4], BBB[4][N];
    double a[N], Be[4], Ba[4];

    double q0, q1, q2, p, q, sq, x1, x2;
    double Lsg1[4], Lsg2[4];

    double tmp1, tmp2;
    double X1, Y1, Z1;
    double X2, Y2, Z2;


    if (N < 4 || N > 12) return -1;

    for (i = 0; i < N; i++) {
        rotZ(sats[i].X, sats[i].Y, sats[i].Z, 0.0, B[i], B[i]+1, B[i]+2);
        B[i][3] = sats[i].pseudorange + sats[i].clock_corr;
    }

    if (N == 4) {
        matrix_invert(B, BBB);
    }
    else {
        for (i = 0; i < 4; i++) {
            for (j = 0; j < 4; j++) {
                BtB[i][j] = 0.0;
                for (k = 0; k < N; k++) {
                    BtB[i][j] += B[k][i]*B[k][j];
                }
            }
        }
        matrix_invert(BtB, BBinv);
        for (i = 0; i < 4; i++) {
            for (j = 0; j < N; j++) {
                BBB[i][j] = 0.0;
                for (k = 0; k < 4; k++) {
                    BBB[i][j] += BBinv[i][k]*B[j][k];
                }
            }
        }
    }

    for (i = 0; i < 4; i++) {
        Be[i] = 0.0;
        for (k = 0; k < N; k++) {
            Be[i] += BBB[i][k]*1.0;
        }
    }

    for (i = 0; i < N; i++) {
        a[i] = 0.5 * lorentz(B[i], B[i]);
    }

    for (i = 0; i < 4; i++) {
        Ba[i] = 0.0;
        for (k = 0; k < N; k++) {
            Ba[i] += BBB[i][k]*a[k];
        }
    }

    q2 = lorentz(Be, Be);
    q1 = lorentz(Ba, Be)-1;
    q0 = lorentz(Ba, Ba);

    if (q2 == 0) return -2;

    p = q1/q2;
    q = q0/q2;

    sq = p*p - q;
    if (sq < 0) return -2;

    x1 = -p + sqrt(sq);
    x2 = -p - sqrt(sq);

    for (i = 0; i < 4; i++) {
        Lsg1[i] = x1*Be[i]+Ba[i];
        Lsg2[i] = x2*Be[i]+Ba[i];
    }
    Lsg1[3] = -Lsg1[3];
    Lsg2[3] = -Lsg2[3];


    tmp1 = sqrt( Lsg1[0]*Lsg1[0] + Lsg1[1]*Lsg1[1] + Lsg1[2]*Lsg1[2] );
    tmp2 = sqrt( Lsg2[0]*Lsg2[0] + Lsg2[1]*Lsg2[1] + Lsg2[2]*Lsg2[2] );

    tmp1 = tmp1 - 6371000.0;
    tmp2 = tmp2 - 6371000.0;

    if ( fabs(tmp1) < fabs(tmp2) ) {
        X1 = Lsg1[0]; Y1 = Lsg1[1]; Z1 = Lsg1[2]; *cc1 = Lsg1[3];
        X2 = Lsg2[0]; Y2 = Lsg2[1]; Z2 = Lsg2[2]; *cc2 = Lsg2[3];
    } else {
        X1 = Lsg2[0]; Y1 = Lsg2[1]; Z1 = Lsg2[2]; *cc1 = Lsg2[3];
        X2 = Lsg1[0]; Y2 = Lsg1[1]; Z2 = Lsg1[2]; *cc2 = Lsg1[3];
    }

    rotZ(X1, Y1, Z1, EARTH_ROTATION_RATE*RANGE_ESTIMATE, pos_ecef1, pos_ecef1+1, pos_ecef1+2);
    rotZ(X2, Y2, Z2, EARTH_ROTATION_RATE*RANGE_ESTIMATE, pos_ecef2, pos_ecef2+1, pos_ecef2+2);

    return 0;
}



void GPS_SatelliteClockDriftCorrection(
  const unsigned short transmission_gpsweek,
  const double         transmission_gpstow,
  const unsigned short ephem_week,
  const double         toe,
  const double         toc,
  const double         af0,
  const double         af1,
  const double         af2,
  const double         ecc,
  const double         sqrta,
  const double         delta_n,
  const double         m0,
  const double         tgd,
  double*  clock_correction,
  double*  clock_drift )
{
    int j;

    double tot;
    double tk;
    double tc;
    double d_tr;
    double d_tsv;
    double a;
    double n;
    double M;
    double E;

    tot = transmission_gpsweek*SECONDS_IN_WEEK + transmission_gpstow;
    tk  = tot - (ephem_week*SECONDS_IN_WEEK + toe);
    tc  = tot - (ephem_week*SECONDS_IN_WEEK + toc);

    a = sqrta*sqrta;
    n = sqrt( GRAVITY_CONSTANT / (a*a*a) );
    n += delta_n;

    M = m0 + n*tk;
    E = M;
    for( j = 0; j < 7; j++ ) {
        E = M + ecc * sin(E);
    }

    d_tr = RELATIVISTIC_CLOCK_CORRECTION * ecc * sqrta * sin(E);
    d_tr *= LIGHTSPEED;

    d_tsv = af0 + af1*tc + af2*tc*tc;

    d_tsv -= tgd;

    *clock_correction = d_tsv*LIGHTSPEED + d_tr;

    *clock_drift = (af1 + 2.0*af2*tc) * LIGHTSPEED;

}

void GPS_ComputeSatellitePositionVelocity(
  const unsigned short transmission_gpsweek,
  const double         transmission_gpstow,
  const unsigned short ephem_week,
  const double         toe,
  const double         m0,
  const double         delta_n,
  const double         ecc,
  const double         sqrta,
  const double         omega0,
  const double         i0,
  const double         w,
  const double         omegadot,
  const double         idot,
  const double         cuc,
  const double         cus,
  const double         crc,
  const double         crs,
  const double         cic,
  const double         cis,
  double* x,
  double* y,
  double* z,
  double* vx,
  double* vy,
  double* vz )
{
    int j;

    double tot;
    double tk;
    double a;
    double n;
    double M;
    double E;
    double v;
    double u;
    double r;
    double i;
    double cos2u;
    double sin2u;
    double d_u;
    double d_r;
    double d_i;
    double x_op;
    double y_op;
    double omegak;
    double cos_omegak;
    double sin_omegak;
    double cosu;
    double sinu;
    double cosi;
    double sini;
    double cosE;
    double sinE;


    tot = transmission_gpsweek*SECONDS_IN_WEEK + transmission_gpstow;
    tk  = tot - (ephem_week*SECONDS_IN_WEEK + toe);

    a = sqrta*sqrta;
    n = sqrt( GRAVITY_CONSTANT / (a*a*a) );
    n += delta_n;

    M = m0 + n*tk;
    E = M;
    for( j = 0; j < 7; j++ ) {
        E = M + ecc * sin(E);
    }

    cosE = cos(E);
    sinE = sin(E);

    v = atan2( (sqrt(1.0 - ecc*ecc)*sinE),  (cosE - ecc) );
    u = v + w;
    r = a * (1.0 - ecc * cos(E));
    i = i0;

    cos2u = cos(2.0*u);
    sin2u = sin(2.0*u);
    d_u = cuc * cos2u  +  cus * sin2u;
    d_r = crc * cos2u  +  crs * sin2u;
    d_i = cic * cos2u  +  cis * sin2u;

    u += d_u;
    r += d_r;
    i += d_i + idot * tk;

    cosu = cos(u);
    sinu = sin(u);
    x_op = r * cosu;
    y_op = r * sinu;


    omegak = omega0 + omegadot*tk - EARTH_ROTATION_RATE*(tk + toe);

    cos_omegak = cos(omegak);
    sin_omegak = sin(omegak);
    cosi = cos(i);
    sini = sin(i);

    *x = x_op * cos_omegak - y_op * sin_omegak * cosi;
    *y = x_op * sin_omegak + y_op * cos_omegak * cosi;
    *z = y_op * sini;




  double omegadotk;
  double edot;
  double vdot;
  double udot;
  double idotdot;
  double rdot;
  double tmpa;
  double tmpb;
  double vx_op;
  double vy_op;

  cos2u = cos(2.0*u);
  sin2u = sin(2.0*u);

  edot  = n / (1.0 - ecc*cosE);
  vdot  = sinE*edot*(1.0 + ecc*cos(v)) / ( sin(v)*(1.0-ecc*cosE) );
  udot  = vdot + 2.0*(cus*cos2u - cuc*sin2u)*vdot;
  rdot  = a*ecc*sinE*n/(1.0-ecc*cosE) + 2.0*(crs*cos2u - crc*sin2u)*vdot;
  idotdot = idot + (cis*cos2u - cic*sin2u)*2.0*vdot;

  vx_op = rdot*cosu - y_op*udot;
  vy_op = rdot*sinu + x_op*udot;

  omegadotk =  omegadot - EARTH_ROTATION_RATE    ;

  tmpa = vx_op - y_op*cosi*omegadotk;
  tmpb = x_op*omegadotk + vy_op*cosi - y_op*sini*idotdot;

  *vx = tmpa * cos_omegak - tmpb * sin_omegak;
  *vy = tmpa * sin_omegak + tmpb * cos_omegak;
  *vz = vy_op*sini + y_op*cosi*idotdot;
}

void GPS_SatellitePositionVelocity_Ephem(
  const unsigned short gpsweek,
  const double         gpstow,
  EPHEM_t ephem,
  double* clock_correction,
  double* clock_drift,
  double* satX,
  double* satY,
  double* satZ,
  double* satvX,
  double* satvY,
  double* satvZ
  )
{
    double tow;
    double x;
    double y;
    double z;
    double vx;
    double vy;
    double vz;
    unsigned short week;


    x = y = z = 0.0;

    GPS_SatelliteClockDriftCorrection( gpsweek, gpstow,
        ephem.week, ephem.toe, ephem.toc, ephem.af0,
        ephem.af1, ephem.af2, ephem.e, ephem.sqrta,
        ephem.delta_n, ephem.M0, ephem.tgd, clock_correction, clock_drift );


    week = gpsweek;
    tow = gpstow + (*clock_correction)/LIGHTSPEED;
    if ( tow < 0.0 ) {
        tow += SECONDS_IN_WEEK;
        week--;
    }
    if ( tow > SECONDS_IN_WEEK ) {
        tow -= SECONDS_IN_WEEK;
        week++;
    }

    GPS_ComputeSatellitePositionVelocity( week, tow,
        ephem.week, ephem.toe, ephem.M0, ephem.delta_n, ephem.e, ephem.sqrta,
        ephem.Omega0, ephem.i0, ephem.w, ephem.OmegaDot, ephem.idot,
        ephem.cuc, ephem.cus, ephem.crc, ephem.crs, ephem.cic, ephem.cis,
        &x, &y, &z, &vx, &vy, &vz );

    *satX = x;
    *satY = y;
    *satZ = z;
    *satvX = vx;
    *satvY = vy;
    *satvZ = vz;

}



double NAV_relVel(LOC_t loc, VEL_t vel) {
    double d;
    double x,y,z;
    double norm;

    x = vel.X-loc.X;
    y = vel.Y-loc.Y;
    z = vel.Z-loc.Z;
    norm = sqrt(x*x+y*y+z*z);
    x /= norm;
    y /= norm;
    z /= norm;

    d = vel.vX*x + vel.vY*y + vel.vZ*z;

    return d;
}

int NAV_LinP(int N, SAT_t satv[], double pos_ecef[3], double dt,
                    double dpos_ecef[3], double *cc) {

    int i, j, k;
    double B[N][4], Binv[4][N], BtB[4][4], BBinv[4][4];
    double a[N], Ba[N];

    double X, Y, Z;
    double norm[N];
    double range, obs_range, prox_range;

    if (N < 4 || N > 12) return -1;

    for (i = 0; i < N; i++) {

        range = dist( pos_ecef[0], pos_ecef[1], pos_ecef[2], satv[i].X, satv[i].Y, satv[i].Z );
        range /= LIGHTSPEED;
        if (range < 0.06  ||  range > 0.1) range = RANGE_ESTIMATE;
        rotZ(satv[i].X, satv[i].Y, satv[i].Z, EARTH_ROTATION_RATE*range, B[i], B[i]+1, B[i]+2);

        X = B[i][0]-pos_ecef[0];
        Y = B[i][1]-pos_ecef[1];
        Z = B[i][2]-pos_ecef[2];
        norm[i] = sqrt(X*X+Y*Y+Z*Z);

        B[i][0] = X/norm[i];
        B[i][1] = Y/norm[i];
        B[i][2] = Z/norm[i];

        B[i][3] = 1;
    }

    if (N == 4) {
        matrix_invert(B, Binv);
    }
    else {

        for (i = 0; i < 4; i++) {
            for (j = 0; j < 4; j++) {
                BtB[i][j] = 0.0;
                for (k = 0; k < N; k++) {
                    BtB[i][j] += B[k][i]*B[k][j];
                }
            }
        }
        matrix_invert(BtB, BBinv);
        for (i = 0; i < 4; i++) {
            for (j = 0; j < N; j++) {
                Binv[i][j] = 0.0;
                for (k = 0; k < 4; k++) {
                    Binv[i][j] += BBinv[i][k]*B[j][k];
                }
            }
        }

    }


    for (i = 0; i < N; i++) {
        obs_range = satv[i].pseudorange + satv[i].clock_corr;
        prox_range = norm[i] - dt;
        a[i] = prox_range - obs_range;
    }

    for (i = 0; i < 4; i++) {
        Ba[i] = 0.0;
        for (k = 0; k < N; k++) {
            Ba[i] += Binv[i][k]*a[k];
        }
    }

    dpos_ecef[0] = Ba[0];
    dpos_ecef[1] = Ba[1];
    dpos_ecef[2] = Ba[2];

    *cc = Ba[3];

    return 0;
}

int NAV_LinV(int N, SAT_t satv[], double pos_ecef[3],
                    double vel_ecef[3], double dt,
                    double dvel_ecef[3], double *cc) {

    int i, j, k;
    double B[N][4], Binv[4][N], BtB[4][4], BBinv[4][4];
    double a[N], Ba[N];

    double X, Y, Z;
    double norm[N];
    double v_proj;
    double obs_rate, prox_rate;
    LOC_t loc;
    VEL_t vel;

    if (N < 4 || N > 12) return -1;

    loc.X = pos_ecef[0];
    loc.Y = pos_ecef[1];
    loc.Z = pos_ecef[2];

    if (N < 4 || N > 12) return -1;

    for (i = 0; i < N; i++) {
        rotZ(satv[i].X, satv[i].Y, satv[i].Z, EARTH_ROTATION_RATE*RANGE_ESTIMATE, B[i], B[i]+1, B[i]+2);

        X = B[i][0]-pos_ecef[0];
        Y = B[i][1]-pos_ecef[1];
        Z = B[i][2]-pos_ecef[2];
        norm[i] = sqrt(X*X+Y*Y+Z*Z);
        B[i][0] = X/norm[i];
        B[i][1] = Y/norm[i];
        B[i][2] = Z/norm[i];


        B[i][3] = 1;
    }

    if (N == 4) {
        matrix_invert(B, Binv);
    }
    else {

        for (i = 0; i < 4; i++) {
            for (j = 0; j < 4; j++) {
                BtB[i][j] = 0.0;
                for (k = 0; k < N; k++) {
                    BtB[i][j] += B[k][i]*B[k][j];
                }
            }
        }
        matrix_invert(BtB, BBinv);
        for (i = 0; i < 4; i++) {
            for (j = 0; j < N; j++) {
                Binv[i][j] = 0.0;
                for (k = 0; k < 4; k++) {
                    Binv[i][j] += BBinv[i][k]*B[j][k];
                }
            }
        }

    }


    for (i = 0; i < N; i++) {
        obs_rate = satv[i].pseudorate;
        vel.X = satv[i].X;
        vel.Y = satv[i].Y;
        vel.Z = satv[i].Z;
        vel.vX = satv[i].vX - vel_ecef[0];
        vel.vY = satv[i].vY - vel_ecef[1];
        vel.vZ = satv[i].vZ - vel_ecef[2];
        v_proj = NAV_relVel(loc, vel);
        prox_rate = v_proj - dt;
        a[i] = prox_rate - obs_rate;
    }

    for (i = 0; i < 4; i++) {
        Ba[i] = 0.0;
        for (k = 0; k < N; k++) {
            Ba[i] += Binv[i][k]*a[k];
        }
    }

    dvel_ecef[0] = Ba[0];
    dvel_ecef[1] = Ba[1];
    dvel_ecef[2] = Ba[2];

    *cc = Ba[3];

    return 0;
}

