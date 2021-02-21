### Source files and search directory
CSRC    = sdboot.c ff.c sdmm.c
P2CC ?= flexspin -2

default: sdboot.binary

sdboot.binary: $(CSRC)
	$(P2CC) -o sdboot.binary $(CSRC)
