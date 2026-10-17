.RECIPEPREFIX := >

PROGRAMS := rs41mod dfm09mod m10m20mod imet4iq meisei100mod c50iq windsondmod rd94rd41drop rs92mod lms6Xmod mp3h1mod mts01mod cf06ht03mod dft_detect

LDLIBS := -lm
CC := gcc
CFLAGS := -O3

# dft_detect runs for every scan candidate. Built against FFTW its correlation
# FFTs need about 5x fewer instructions than the built-in radix-2 FFT.
# libfftw3-dev is installed for KA9Q anyway; without it the built-in FFT is used.
# WSRX_FFTW=0 forces the built-in FFT.
ifeq ($(filter clean,$(MAKECMDGOALS)),)
ifndef WSRX_FFTW
WSRX_FFTW := $(shell echo 'int main(void){fftwf_free(fftwf_malloc(8));return 0;}' | $(CC) -x c -include fftw3.h - -o /dev/null -lfftw3f >/dev/null 2>&1 && echo 1 || echo 0)
endif
ifeq ($(WSRX_FFTW),1)
$(info dft_detect: using FFTW (libfftw3f))
DFT_DETECT_DEFS := -DHAVE_FFTW3
DFT_DETECT_LIBS := -lfftw3f
else
$(info dft_detect: using the slower built-in FFT (libfftw3f not found or WSRX_FFTW=0). For FFTW: sudo apt install libfftw3-dev, then make clean && make)
endif
endif

.PHONY: all clean

all: $(PROGRAMS)

rs41mod: rs41mod.o demod_mod.o bch_ecc_mod.o

dfm09mod: dfm09mod.o demod_mod.o

m10m20mod: m10m20mod.o demod_mod.o

rs92mod: rs92mod.o demod_mod.o bch_ecc_mod.o 

c50iq: c50iq.o demod_mod.o

meisei100mod: meisei100mod.o demod_mod.o bch_ecc_mod.o

lms6Xmod: lms6Xmod.o demod_mod.o bch_ecc_mod.o

mp3h1mod: mp3h1mod.o demod_mod.o

mts01mod: mts01mod.o demod_mod.o

cf06ht03mod: cf06ht03mod.o demod_mod.o bch_ecc_mod.o

imet4iq: imet4iq.c
>$(CC) $(CFLAGS) imet4iq.c -lm -o imet4iq

windsondmod: windsondmod.o 

rd94rd41drop: rd94rd41drop.c
>$(CC) $(CFLAGS) rd94rd41drop.c -lm -o rd94rd41drop

dft_detect: dft_detect.c
>$(CC) -Ofast -march=native $(DFT_DETECT_DEFS) dft_detect.c $(DFT_DETECT_LIBS) -lm -o dft_detect

demod_mod.o: CFLAGS += -Ofast

demod_mod.o: demod_mod.h

bch_ecc_mod.o: bch_ecc_mod.h

clean:
>rm -f $(PROGRAMS) $(PROGRAMS:=.o)
>rm -f demod_mod.o bch_ecc_mod.o
