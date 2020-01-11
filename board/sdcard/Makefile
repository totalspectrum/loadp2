### Source files and search directory
CSRC    = sdboot.c ff.c sdmm.c
P2CC ?= fastspin -2

default: sdboot.binary

sdboot.binary: $(CSRC)
	$(P2CC) -o sdboot.binary $(CSRC)
