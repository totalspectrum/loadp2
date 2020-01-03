#
# Makefile for loadp2
# Added by Eric Smith -- ugliness or flaws here are my fault, not Dave Hein's!
#

# if CROSS is defined, we are building a cross compiler
# possible targets are: win32, rpi
# Note that you may have to adjust your compiler names depending on
# which Linux distribution you are using (e.g. ubuntu uses
# "i586-mingw32msvc-gcc" for mingw, whereas Debian uses
# "i686-w64-mingw32-gcc"
#
ifeq ($(CROSS),win32)
#  CC=i586-mingw32msvc-gcc
  CC=i686-w64-mingw32-gcc
  EXT=.exe
  BUILD=./build-win32
  OSFILE=osint_mingw.c
else ifeq ($(CROSS),rpi)
  CC=arm-linux-gnueabihf-gcc
  EXT=
  BUILD=./build-rpi
  OSFILE=osint_linux.c
else ifeq ($(CROSS),linux32)
  CC=gcc -m32
  EXT=
  BUILD=./build-linux32
  OSFILE=osint_linux.c
else ifeq ($(CROSS),macosx)
  CC=o64-clang -DMACOSX
  EXT=
  BUILD=./build-macosx
  OSFILE=osint_linux.c
else
  CC=gcc
  EXT=
  BUILD=./build
  OSFILE=osint_linux.c
endif

# check for MACs
ifeq ($(shell uname -s),Darwin)
  DEFS=-DMACOSX
else
  DEFS=
endif

# board support programs
BOARDS=board/P2ES_flashloader.bin

# program for converting MainLoader.spin2 to MainLoader.binary
PASM=fastspin -2

default: $(BUILD)/loadp2$(EXT) $(BOARDS)

HEADERS=MainLoader.h MainLoader1.h

U9FS=u9fs/u9fs.c u9fs/authnone.c u9fs/print.c u9fs/doprint.c u9fs/rune.c u9fs/fcallconv.c u9fs/dirmodeconv.c u9fs/convM2D.c u9fs/convS2M.c u9fs/convD2M.c u9fs/convM2S.c u9fs/readn.c

$(BUILD)/loadp2$(EXT): $(BUILD) loadp2.c loadelf.c loadelf.h osint_linux.c osint_mingw.c $(HEADERS) $(U9FS)
	$(CC) -Wall -O -g $(DEFS) -o $@ loadp2.c loadelf.c $(OSFILE) $(U9FS)

clean:
	rm -rf $(BUILD) *.o $(HEADERS) *.pasm *.bin

$(BUILD):
	mkdir -p $(BUILD)

%.h: %.bin
	xxd -i $< > $@

%.bin: %.spin2
	$(PASM) -o $@ $<
