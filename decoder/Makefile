PROGRAMS := rs41mod dfm09mod m10m20mod imet4iq dft_detect

LDLIBS = -lm
CC = gcc
CFLAGS = -O3

.PHONY: all
all: $(PROGRAMS)

rs41mod: rs41mod.o demod_mod.o bch_ecc_mod.o

dfm09mod: dfm09mod.o demod_mod.o

m10m20mod: m10m20mod.o demod_mod.o

demod_mod.o: CFLAGS += -Ofast

demod_mod.o: demod_mod.h

bch_ecc_mod.o: bch_ecc_mod.h

.PHONY: clean
clean:
	rm -f $(PROGRAMS) $(PROGRAMS:=.o)
	rm -f demod_mod.o
	rm -f bch_ecc_mod.o

