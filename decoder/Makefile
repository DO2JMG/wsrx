.RECIPEPREFIX := >

PROGRAMS := rs41mod dfm09mod m10m20mod imet4iq meisei100mod c50iq windsondmod dft_detect

LDLIBS := -lm
CC := gcc
CFLAGS := -O3

.PHONY: all clean

all: $(PROGRAMS)

rs41mod: rs41mod.o demod_mod.o bch_ecc_mod.o

dfm09mod: dfm09mod.o demod_mod.o

m10m20mod: m10m20mod.o demod_mod.o

c50iq: c50iq.o demod_mod.o

meisei100mod: meisei100mod.o demod_mod.o bch_ecc_mod.o

imet4iq: imet4iq.c
>$(CC) $(CFLAGS) imet4iq.c -lm -o imet4iq

windsondmod: windsondmod.o 

dft_detect: dft_detect.c
>$(CC) -Ofast -march=native dft_detect.c -lm -o dft_detect

demod_mod.o: CFLAGS += -Ofast

demod_mod.o: demod_mod.h

bch_ecc_mod.o: bch_ecc_mod.h

clean:
>rm -f $(PROGRAMS) $(PROGRAMS:=.o)
>rm -f demod_mod.o bch_ecc_mod.o
