


#include "bch_ecc_mod.h"




static int GF_deg(ui32_t p) {
    ui32_t d = 31;
    if (p == 0) return -1;
    else {
        while (d && !(p & (1<<d)))  d--;
    }
    return d;
}

static ui8_t GF2m_mul(GF_t *gf, ui8_t a, ui8_t b) {
    ui32_t aa = a;
    ui8_t ab = 0;
    int i, m = GF_deg(b);

    if (b & 1) ab = a;

    for (i = 0; i < m; i++) {
        aa = (aa << 1);
        if (GF_deg(aa) == GF_deg(gf->f))
             aa ^= gf->f;
        b >>= 1;
        if (b & 1) ab ^= (ui8_t)aa;
    }
    return ab;
}

static int GF_genTab(GF_t *gf) {
    int i, j;
    ui8_t b;



    b = 0x01;
    for (i = 0; i < gf->ord; i++) {
        gf->exp_a[i] = b;
        b = GF2m_mul(gf, gf->alpha, b);
    }

    gf->log_a[0] = -00;
    for (i = 1; i < gf->ord; i++) {
        b = 0x01; j = 0;
        while (b != i) {
            b = GF2m_mul(gf, gf->alpha, b);
            j++;
            if (j > gf->ord-1) {
                return -1;
            }
        }
        gf->log_a[i] = j;
    }

    return 0;
}



static ui8_t GF_mul(GF_t *gf, ui8_t p, ui8_t q) {
  ui32_t x;
  if ((p == 0) || (q == 0)) return 0;
  x = (ui32_t)gf->log_a[p] + gf->log_a[q];
  return gf->exp_a[x % (gf->ord-1)];
}

static ui8_t GF_inv(GF_t *gf, ui8_t p) {
  if (p == 0) return 0;
  return gf->exp_a[gf->ord-1-gf->log_a[p]];
}



static ui8_t poly_eval(GF_t *gf, ui8_t poly[], ui8_t x) {
    int n;
    ui8_t xn, y;

    y = poly[0];
    if (x != 0) {
        for (n = 1; n < gf->ord-1; n++) {
            xn = gf->exp_a[(n*gf->log_a[x]) % (gf->ord-1)];
            y ^= GF_mul(gf, poly[n], xn);
        }
    }
    return y;
}

static ui8_t poly_evalH(GF_t *gf, ui8_t poly[], ui8_t x) {
    int n;
    ui8_t y;
    y = poly[gf->ord-1];
    for (n = gf->ord-2; n >= 0; n--) {
        y = GF_mul(gf, y, x) ^ poly[n];
    }
    return y;
}


static int poly_deg(ui8_t p[]) {
    int n = MAX_DEG;
    while (p[n] == 0 && n > 0) n--;
    if (p[n] == 0) n--;
    return n;
}

static int poly_divmod(GF_t *gf, ui8_t p[], ui8_t q[], ui8_t *d, ui8_t *r) {
  int deg_p, deg_q;
  int i;
  ui8_t c;

  deg_p = poly_deg(p);
  deg_q = poly_deg(q);

  if (deg_q < 0) return -1;

  for (i = 0; i <= MAX_DEG; i++) d[i] = 0;
  for (i = 0; i <= MAX_DEG; i++) r[i] = 0;


  c = GF_mul(gf, p[deg_p], GF_inv(gf, q[deg_q]));

  if (deg_q == 0) {
      for (i = 0; i <= deg_p;   i++) d[i] = GF_mul(gf, p[i], c);
      for (i = 0; i <= MAX_DEG; i++) r[i] = 0;
  }
  else if (deg_p < 0) {
      for (i = 0; i <= MAX_DEG; i++) {
          d[i] = 0;
          r[i] = 0;
      }
  }
  else if (deg_p < deg_q) {
      for (i = 0; i <= MAX_DEG; i++) d[i] = 0;
      for (i = 0; i <= deg_p; i++) r[i] = p[i];
      for (i = deg_p+1; i <= MAX_DEG; i++) r[i] = 0;
  }
  else {
      for (i = 0; i <= deg_p; i++) r[i] = p[i];
      while (deg_p >= deg_q) {
          d[deg_p-deg_q] = c;
          for (i = 0; i <= deg_q; i++) {
              r[deg_p-i] ^= GF_mul(gf, q[deg_q-i], c);
          }
          while (r[deg_p] == 0 && deg_p > 0) deg_p--;
          if (r[deg_p] == 0) deg_p--;
          if (deg_p >= 0) c = GF_mul(gf, r[deg_p], GF_inv(gf, q[deg_q]));
      }
  }
  return 0;
}

static int poly_add(ui8_t a[], ui8_t b[], ui8_t *sum) {
    int i;
    ui8_t c[MAX_DEG+1];

    for (i = 0; i <= MAX_DEG; i++) {
            c[i] = a[i] ^ b[i];
    }

    for (i = 0; i <= MAX_DEG; i++) { sum[i] = c[i]; }

    return 0;
}

static int poly_mul(GF_t *gf, ui8_t a[], ui8_t b[], ui8_t *ab) {
    int i, j;
    ui8_t c[MAX_DEG+1];

    if (poly_deg(a)+poly_deg(b) > MAX_DEG) {
       return -1;
    }

    for (i = 0; i <= MAX_DEG; i++) { c[i] = 0; }

    for (i = 0; i <= poly_deg(a); i++) {
        for (j = 0; j <= poly_deg(b); j++) {
            c[i+j] ^= GF_mul(gf, a[i], b[j]);
        }
    }

    for (i = 0; i <= MAX_DEG; i++) { ab[i] = c[i]; }

    return 0;
}


static int polyGF_eggT(GF_t *gf, int deg, ui8_t a[], ui8_t b[],
                          ui8_t *u, ui8_t *v, ui8_t *ggt
               ) {

    int i;
    ui8_t r0[MAX_DEG+1], r1[MAX_DEG+1], r2[MAX_DEG+1],
          s0[MAX_DEG+1], s1[MAX_DEG+1], s2[MAX_DEG+1],
          t0[MAX_DEG+1], t1[MAX_DEG+1], t2[MAX_DEG+1],
          quo[MAX_DEG+1];

    for (i = 0; i <= MAX_DEG; i++) { u[i] = 0; }
    for (i = 0; i <= MAX_DEG; i++) { v[i] = 0; }
    for (i = 0; i <= MAX_DEG; i++) { ggt[i] = 0; }

    for (i = 0; i <= MAX_DEG; i++) { r0[i] = a[i]; }
    for (i = 0; i <= MAX_DEG; i++) { r1[i] = b[i]; }
    s0[0] = 1; for (i = 1; i <= MAX_DEG; i++) { s0[i] = 0; }
    s1[0] = 0; for (i = 1; i <= MAX_DEG; i++) { s1[i] = 0; }
    t0[0] = 0; for (i = 1; i <= MAX_DEG; i++) { t0[i] = 0; }
    t1[0] = 1; for (i = 1; i <= MAX_DEG; i++) { t1[i] = 0; }
    for (i = 0; i <= MAX_DEG; i++) { r2[i] = 0; }
    for (i = 0; i <= MAX_DEG; i++) { s2[i] = 0; }
    for (i = 0; i <= MAX_DEG; i++) { t2[i] = 0; }

    while ( poly_deg(r1) >= deg ) {
        poly_divmod(gf, r0, r1, quo, r2);
        for (i = 0; i <= MAX_DEG; i++) { r0[i] = r1[i]; }
        for (i = 0; i <= MAX_DEG; i++) { r1[i] = r2[i]; }
        poly_mul(gf, quo, s1, s2);
        poly_add(s0, s2, s2);
        for (i = 0; i <= MAX_DEG; i++) { s0[i] = s1[i]; }
        for (i = 0; i <= MAX_DEG; i++) { s1[i] = s2[i]; }
        poly_mul(gf, quo, t1, t2);
        poly_add(t0, t2, t2);
        for (i = 0; i <= MAX_DEG; i++) { t0[i] = t1[i]; }
        for (i = 0; i <= MAX_DEG; i++) { t1[i] = t2[i]; }
    }

    if (deg > 0) {
        for (i = 0; i <= MAX_DEG; i++) { ggt[i] = r1[i]; }
        for (i = 0; i <= MAX_DEG; i++) { u[i] = s1[i]; }
        for (i = 0; i <= MAX_DEG; i++) { v[i] = t1[i]; }
    }
    else {
        for (i = 0; i <= MAX_DEG; i++) { ggt[i] = r0[i]; }
        for (i = 0; i <= MAX_DEG; i++) { u[i] = s0[i]; }
        for (i = 0; i <= MAX_DEG; i++) { v[i] = t0[i]; }
    }

    return 0;
}

static int polyGF_lfsr(GF_t *gf, int deg, int x2t, ui8_t S[],
                          ui8_t *Lambda, ui8_t *Omega ) {
    int i;
    ui8_t r0[MAX_DEG+1], r1[MAX_DEG+1], r2[MAX_DEG+1],
          s0[MAX_DEG+1], s1[MAX_DEG+1], s2[MAX_DEG+1],
          quo[MAX_DEG+1];

    for (i = 0; i <= MAX_DEG; i++) { Lambda[i] = 0; }
    for (i = 0; i <= MAX_DEG; i++) { Omega[i] = 0; }

    for (i = 0; i <= MAX_DEG; i++) { r0[i] = S[i]; }
    for (i = 0; i <= MAX_DEG; i++) { r1[i] = 0; } r1[x2t] = 1;
    s0[0] = 1; for (i = 1; i <= MAX_DEG; i++) { s0[i] = 0; }
    s1[0] = 0; for (i = 1; i <= MAX_DEG; i++) { s1[i] = 0; }
    for (i = 0; i <= MAX_DEG; i++) { r2[i] = 0; }
    for (i = 0; i <= MAX_DEG; i++) { s2[i] = 0; }

    while ( poly_deg(r1) >= deg ) {
        poly_divmod(gf, r0, r1, quo, r2);
        for (i = 0; i <= MAX_DEG; i++) { r0[i] = r1[i]; }
        for (i = 0; i <= MAX_DEG; i++) { r1[i] = r2[i]; }

        poly_mul(gf, quo, s1, s2);
        poly_add(s0, s2, s2);
        for (i = 0; i <= MAX_DEG; i++) { s0[i] = s1[i]; }
        for (i = 0; i <= MAX_DEG; i++) { s1[i] = s2[i]; }
    }

    for (i = 0; i <= MAX_DEG; i++) { Omega[i]  = r1[i]; }
    for (i = 0; i <= MAX_DEG; i++) { Lambda[i] = s1[i]; }

    return 0;
}

static int poly_D(ui8_t a[], ui8_t *Da) {
    int i;

    for (i = 0; i <= MAX_DEG; i++) { Da[i] = 0; }
    for (i = 1; i <= poly_deg(a); i++) {
        if (i % 2) Da[i-1] = a[i];
    }

    return 0;
}

static ui8_t forney(RS_t *RS, ui8_t x, ui8_t Omega[], ui8_t Lambda[]) {
    GF_t *gf = &RS->GF;
    ui8_t DLam[MAX_DEG+1];
    ui8_t w, z, Y;
    poly_D(Lambda, DLam);
    w = poly_eval(gf, Omega, x);
    z = poly_eval(gf, DLam, x); if (z == 0) { return -00; }
    Y = GF_mul(gf, w, GF_inv(gf, z));
    if (RS->b == 0) Y = GF_mul(gf, GF_inv(gf, x), Y);
    else if (RS->b > 1) {
        ui8_t xb1 = gf->exp_a[((RS->b-1)*gf->log_a[x]) % (gf->ord-1)];
        Y = GF_mul(gf, xb1, Y);
    }

    return Y;
}

static int era_sigma(RS_t *RS, int n, ui8_t era_pos[], ui8_t *sigma) {
    GF_t *gf = &(RS->GF);
    int i;
    ui8_t Xa[MAX_DEG+1], sig[MAX_DEG+1];
    ui8_t a_i;

    for (i = 0; i <= MAX_DEG; i++) sig[i] = 0;
    for (i = 0; i <= MAX_DEG; i++) Xa[i] = 0;

    sig[0] = 1;
    Xa[0] = 1;
    for (i = 0; i < n; i++) {
        a_i = gf->exp_a[(RS->p*era_pos[i]) % (gf->ord-1)];
        Xa[1] = a_i;
        poly_mul(gf, sig, Xa, sig);
    }

    for (i = 0; i <= MAX_DEG; i++) sigma[i] = sig[i];

    return 0;
}

static int syndromes(RS_t *RS, ui8_t cw[], ui8_t *S) {
    GF_t *gf = &RS->GF;
    int i, errors = 0;
    ui8_t a_i;

    for (i = 0; i < 2*RS->t; i++) {
        a_i = gf->exp_a[(RS->p*(RS->b+i)) % (gf->ord-1)];
        S[i] = poly_eval(gf, cw, a_i);
        if (S[i]) errors = 1;
    }
    return errors;
}



INCSTAT
int rs_init_RS(RS_t *RS) {
    GF_t *gf = &RS->GF;
    int i, check_gen;
    ui8_t Xalp[MAX_DEG+1];

    check_gen = GF_genTab(gf);

    for (i = 0; i <= MAX_DEG; i++) RS->g[i] = 0;
    for (i = 0; i <= MAX_DEG; i++) Xalp[i] = 0;

    for (i = 1; i < gf->ord-1; i++) {
        if ( (RS->p * i) % (gf->ord-1) == 1 ) {
            RS->ip = i;
            break;
        }
    }

    RS->g[0] = 0x01;
    Xalp[1] = 0x01;
    for (i = 0; i < 2*RS->t; i++) {
        Xalp[0] = gf->exp_a[(RS->p*(RS->b+i)) % (gf->ord-1)];
        poly_mul(gf, RS->g, Xalp, RS->g);
    }

    return check_gen;
}

INCSTAT
int rs_init_RS255(RS_t *RS) {
    GF_t *gf = &RS->GF;
    int i, check_gen;
    ui8_t Xalp[MAX_DEG+1];

    *RS = RS256;
    *gf = GF256RS;
    check_gen = GF_genTab(gf);

    for (i = 0; i <= MAX_DEG; i++) RS->g[i] = 0;
    for (i = 0; i <= MAX_DEG; i++) Xalp[i] = 0;

    RS->g[0] = 0x01;
    Xalp[1] = 0x01;
    for (i = 0; i < 2*RS->t; i++) {
        Xalp[0] = gf->exp_a[(RS->b+i) % (gf->ord-1)];
        poly_mul(gf, RS->g, Xalp, RS->g);
    }

    return check_gen;
}

INCSTAT
int rs_init_RS255ccsds(RS_t *RS) {
    GF_t *gf = &RS->GF;
    int i, check_gen;
    ui8_t Xalp[MAX_DEG+1];

    *RS = RS256ccsds;
    *gf = GF256RSccsds;
    check_gen = GF_genTab(gf);

    for (i = 0; i <= MAX_DEG; i++) RS->g[i] = 0;
    for (i = 0; i <= MAX_DEG; i++) Xalp[i] = 0;

    for (i = 1; i < gf->ord-1; i++) {
        if ( (RS->p * i) % (gf->ord-1) == 1 ) {
            RS->ip = i;
            break;
        }
    }

    RS->g[0] = 0x01;
    Xalp[1] = 0x01;
    for (i = 0; i < 2*RS->t; i++) {
        Xalp[0] = gf->exp_a[(RS->p*(RS->b+i)) % (gf->ord-1)];
        poly_mul(gf, RS->g, Xalp, RS->g);
    }
    return check_gen;
}

INCSTAT
int rs_init_BCH64(RS_t *RS) {
    GF_t *gf = &RS->GF;
    int i, check_gen;

    *RS = BCH64;
    *gf = GF64BCH;
    check_gen = GF_genTab(gf);

    for (i = 0; i <= MAX_DEG; i++) RS->g[i] = 0;

    RS->g[0] = RS->g[3] = RS->g[4] = RS->g[5] = RS->g[8] = RS->g[10] = RS->g[12] = 1;

    return check_gen;
}

INCSTAT
int rs_init_RS15ccsds(RS_t *RS) {
    GF_t *gf = &RS->GF;
    int i, check_gen;
    ui8_t Xalp[MAX_DEG+1];

    *RS = RS16ccsds;
    *gf = GF16RS;
    check_gen = GF_genTab(gf);

    for (i = 0; i <= MAX_DEG; i++) RS->g[i] = 0;
    for (i = 0; i <= MAX_DEG; i++) Xalp[i] = 0;

    RS->g[0] = 0x01;
    Xalp[1] = 0x01;
    for (i = 0; i < 2*RS->t; i++) {
        Xalp[0] = gf->exp_a[(RS->b+i) % (gf->ord-1)];
        poly_mul(gf, RS->g, Xalp, RS->g);
    }

    return check_gen;
}

INCSTAT
int rs_encode(RS_t *RS, ui8_t cw[]) {
    GF_t *gf = &RS->GF;
    int j;
    ui8_t __cw[MAX_DEG+1],
          parity[MAX_DEG+1],
          d[MAX_DEG+1];
    for (j = 0; j <= MAX_DEG; j++) parity[j] = 0;
    for (j = 0; j <= MAX_DEG; j++) __cw[j] = 0;
    for (j = RS->R; j < RS->N; j++) __cw[j] = cw[j];
    poly_divmod(gf, __cw, RS->g, d, parity);
    for (j = 0; j < RS->R; j++) cw[j] = parity[j];
    return 0;
}

INCSTAT
int rs_decode_ErrEra(RS_t *RS, ui8_t cw[], int nera, ui8_t era_pos[],
                               ui8_t *err_pos, ui8_t *err_val) {
    GF_t *gf = &RS->GF;
    ui8_t x, gamma;
    ui8_t S[MAX_DEG+1],
          Lambda[MAX_DEG+1],
          Omega[MAX_DEG+1],
          sigma[MAX_DEG+1],
          sigLam[MAX_DEG+1];
    int deg_sigLam, deg_Lambda, deg_Omega;
    int i, nerr, errera = 0;

    if (nera > 2*RS->t) { return -4; }

    for (i = 0; i < 2*RS->t; i++) { err_pos[i] = 0; }
    for (i = 0; i < 2*RS->t; i++) { err_val[i] = 0; }


    for (i = 0; i <= MAX_DEG; i++) { S[i] = 0; }
    errera = syndromes(RS, cw, S);

    for (i = 0; i <= MAX_DEG; i++) { sigma[i] = 0; }
    sigma[0] = 1;


    if (nera > 0) {
        era_sigma(RS, nera, era_pos, sigma);
        poly_mul(gf, sigma, S, S);
        for (i = 2*RS->t; i <= MAX_DEG; i++) S[i] = 0;
    }

    if (errera)
    {
        polyGF_lfsr(gf, RS->t+nera/2, 2*RS->t, S, Lambda, Omega);

        deg_Lambda = poly_deg(Lambda);
        deg_Omega  = poly_deg(Omega);
        if (deg_Omega >= deg_Lambda + nera) {
            errera = -3;
            return errera;
        }
        gamma = Lambda[0];
        if (gamma) {
            for (i = deg_Lambda; i >= 0; i--) Lambda[i] = GF_mul(gf, Lambda[i], GF_inv(gf, gamma));
            for (i = deg_Omega ; i >= 0; i--)  Omega[i] = GF_mul(gf,  Omega[i], GF_inv(gf, gamma));
            poly_mul(gf, sigma, Lambda, sigLam);
            deg_sigLam = poly_deg(sigLam);
        }
        else {
            errera = -2;
            return errera;
        }

        nerr = 0;
        for (i = 1; i < gf->ord ; i++) {
            x = (ui8_t)i;
            if (poly_eval(gf, sigLam, x) == 0) {
                ui8_t x1 = GF_inv(gf, x);
                err_pos[nerr] = (gf->log_a[x1]*RS->ip) % (gf->ord-1);
                err_val[nerr] = forney(RS, x, Omega, sigLam);
                nerr++;
            }
            if (nerr >= deg_sigLam) break;
        }

        if (nerr < deg_sigLam) errera = -1;
        else {
            errera = nerr;
            for (i = 0; i < errera; i++) cw[err_pos[i]] ^= err_val[i];
        }
    }

    return errera;
}

INCSTAT
int rs_decode(RS_t *RS, ui8_t cw[], ui8_t *err_pos, ui8_t *err_val) {
    ui8_t tmp[1] = {0};
    return rs_decode_ErrEra(RS, cw, 0, tmp, err_pos, err_val);
}

INCSTAT
int rs_decode_bch_gf2t2(RS_t *RS, ui8_t cw[], ui8_t *err_pos, ui8_t *err_val) {
    GF_t *gf = &RS->GF;
    ui8_t x, gamma,
          S[MAX_DEG+1],
          L[MAX_DEG+1], L2,
          Lambda[MAX_DEG+1],
          Omega[MAX_DEG+1];
    int i, n, errors = 0;


    for (i = 0; i < RS->t; i++) { err_pos[i] = 0; }
    for (i = 0; i < RS->t; i++) { err_val[i] = 0; }

    for (i = 0; i <= MAX_DEG; i++) { S[i] = 0; }
    errors = syndromes(RS, cw, S);

    if (errors) {
        polyGF_lfsr(gf, RS->t, 2*RS->t, S, Lambda, Omega);
        gamma = Lambda[0];
        if (gamma) {
            for (i = poly_deg(Lambda); i >= 0; i--) Lambda[i] = GF_mul(gf, Lambda[i], GF_inv(gf, gamma));
            for (i = poly_deg(Omega) ; i >= 0; i--)  Omega[i] = GF_mul(gf,  Omega[i], GF_inv(gf, gamma));
        }
        else {
            errors = -2;
            return errors;
        }

        if ( RS->t == 2 ) {

            for (i = 0; i <= MAX_DEG; i++) { L[i] = 0; }
            L[0] = 1;
            L[1] = S[0];
            L2 = GF_mul(gf, S[0], S[0]); L2 = GF_mul(gf, L2, S[0]); L2 ^= S[2];
            L2 = GF_mul(gf, L2, GF_inv(gf, S[0]));
            L[2] = L2;

            if (S[1] != GF_mul(gf, S[0],S[0]) || S[3] != GF_mul(gf, S[1],S[1])) {
                errors = -2;
                return errors;
            }
            if (L[1] != Lambda[1] || L[2] != Lambda[2] ) {
                errors = -2;
                return errors;
            }
        }

        n = 0;
        for (i = 1; i < gf->ord ; i++) {
            x = (ui8_t)i;
            if (poly_eval(gf, Lambda, x) == 0) {
                err_pos[n] = gf->log_a[GF_inv(gf, x)];
                err_val[n] = 1;
                n++;
            }
            if (n >= poly_deg(Lambda)) break;
        }

        if (n < poly_deg(Lambda)) errors = -1;
        else {
            errors = n;
            for (i = 0; i < errors; i++) cw[err_pos[i]] ^= err_val[i];
        }
    }

    return errors;
}

