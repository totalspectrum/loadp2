/*
 *
 * Copyright (c) 2017-2019 by Dave Hein
 * Copyright (c) 2019-2024 Total Spectrum Software Inc.
 * Based on p2load written by David Betz
 *
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdbool.h>
#include "osint.h"
#include "loadelf.h"

#define ARGV_ADDR  0xFC000
#define ARGV_MAGIC ('A' | ('R' << 8) | ('G'<<16) | ('v'<<24))
#define ARGV_MAX_BYTES 1020
#define ARGC_MAX_ITEMS 32

/* default FIFO size of FT231X in P2-EVAL board and PropPlugs */
//#define DEFAULT_FIFO_SIZE   512
#define DEFAULT_FIFO_SIZE   1024 /* seems to work better */

#define NO_ENTER    0
#define ENTER_TAQOZ 1
#define ENTER_DEBUG 2

#define LOAD_CHIP   0
#define LOAD_FPGA   1
#define LOAD_SINGLE 2
#define LOAD_SPI    3

#define ROUND_UP(x) (((x)+3) & ~3)

static int loader_baud = 2000000;
static int clock_mode = -1;
static int user_baud = 115200;
static int clock_freq = 80000000;
static int extra_cycles = 7;
static int load_mode = -1;
static int patch_mode = 0;
static int use_checksum = 1;
static int quiet_mode = 0;
static int enter_rom = NO_ENTER;
static char *send_script = NULL;
static int mem_argv_bytes = 0;
static char *mem_argv_data = NULL;

int get_loader_baud(int ubaud, int lbaud);
static void RunScript(char *script);

#if defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__)
  #define PORT_PREFIX "com"
  #define INTEGER_PREFIXES
  #include <windows.h>
#elif defined(MACOSX)
  #define PORT_PREFIX "cu.usbserial"
  #include <dirent.h>
#else
  #define PORT_PREFIX "ttyUSB"
  #include <dirent.h>
#endif

#include "MainLoader_fpga.h"
#include "MainLoader_chip.h"
#include "flash_loader.h"
#include "himem_flash.h"

static int32_t ibuf[256];
static int32_t ibin[32];
static char *buffer = (char *)ibuf;
static char *binbuffer = (char *)ibin;
static int verbose = 0;
int waitAtExit = 0;
static int force_zero = 0;  /* default to zeroing memory */
static int do_hwreset = 1;
static int fifo_size = DEFAULT_FIFO_SIZE;

static uint8_t *himem_bin;
static uint32_t himem_size;

int ignoreEof = 0;

/* duplicate a string, useful if our original string might
 * not be modifiable
 */
static char *duplicate_string(const char *str)
{
    size_t len = strlen(str)+1;
    char *r = malloc(len);
    if (r) {
        strcpy(r, str);
    }
    return r;
}

/* promptexit: print a prompt if waitAtExit is set, then exit */
void
promptexit(int r)
{
    int c;
    if (waitAtExit) {
        fflush(stderr);
        printf("Press enter to continue...\n");
        fflush(stdout);
        do {
            c = getchar();
        } while (c > 0 && c != '\n' && c != '\r');
    }
    exit(r);
}

/* Usage - display a usage message and exit */
static void Usage(const char *msg)
{
    if (msg) {
        printf("%s\n", msg);
    }
printf("\
loadp2 - a loader for the propeller 2 - version 0.074 " __DATE__ "\n\
usage: loadp2\n\
         [ -p port ]               serial port\n\
         [ -b baud ]               user baud rate (default is %d)\n\
         [ -l baud ]               loader baud rate (default is %d)\n\
         [ -f clkfreq ]            clock frequency (default is %d)\n\
         [ -m clkmode ]            clock mode in hex (default is %02x)\n\
         [ -s address ]            starting address in hex (default is 0)\n\
         [ -t ]                    enter terminal mode after running the program\n\
         [ -T ]                    enter PST-compatible terminal mode\n\
         [ -v ]                    enable verbose mode\n\
         [ -k ]                    wait for user input before exit\n\
         [ -q ]                    quiet mode: also checks for exit sequence\n\
         [ -n ]                    no reset; skip any hardware reset\n\
         [ -9 dir ]                serve 9p remote filesystem from dir\n\
         [ -FIFO bytes]            modify serial FIFO size (default is %d bytes)\n\
         [ -? ]                    display a usage message and exit\n\
         [ -DTR ]                  use DTR for reset (default)\n\
         [ -RTS ]                  use RTS for reset\n\
         [ -xDEBUG ]               enter ROM debug monitor\n\
         [ -xTAQOZ ]               enter ROM version of TAQOZ\n\
         [ -xTERM ]                enter terminal, avoid reset\n\
         [ -CHIP ]                 set load mode for CHIP\n\
         [ -FPGA ]                 set load mode for FPGA\n\
         [ -NOZERO ]               do not clear memory before download (default)\n\
         [ -ZERO ]                 clear memory before download\n\
         [ -PATCH ]                patch in clock frequency and serial parms\n\
         [ -SINGLE ]               set load mode for single stage\n\
         [ -FLASH ]                like -SINGLE, but copies application to SPI flash\n\
         [ -SPI ]                  alias for -FLASH\n\
         [ -NOEOF ]                ignore EOF on input\n\
         [ -HIMEM=flash ]          addresses 0x8000000 and up refer to flash\n\
         filespec                  file to load\n\
         [ -e script ]             send a sequence of characters after starting P2\n\
         [ -a arg1 [arg2 ...] ]    put arguments for program into memory\n\
", user_baud, loader_baud, clock_freq, clock_mode, DEFAULT_FIFO_SIZE);
printf("\n\
In -CHIP mode, filespec may optionally be multiple files with address\n\
specifiers, such as:\n\
    @ADDR=file1,@ADDR=file2,@ADDR+file3\n\
Here ADDR is a hex address at which to load the next file, followed by = or +\n\
If it is followed by + then the size of the file is put in memory followed by\n\
the file data. This feature is useful for loading data that a program wishes\n\
to act on. For example, a VGA program which displays data from $1000 may be\n\
loaded with:\n\
    @0=vgacode.bin,@1000=picture.bmp\n\
The main executable code must always be specified first\n\
");

promptexit(1);
}

void tx_raw_byte(unsigned int c)
{
    buffer[0] = c & 0xff;
    tx((uint8_t *)buffer, 1);
}

void tx_raw_long(unsigned int size)
{
    buffer[0] = (size >> 0) & 0xff;
    buffer[1] = (size >> 8) & 0xff;
    buffer[2] = (size >> 16) & 0xff;
    buffer[3] = (size >> 24) & 0xff;
    tx((uint8_t *)buffer, 4);
}

void txbyte(int val)
{
    sprintf(buffer, " %2.2x", val&255);
    tx((uint8_t *)buffer, strlen(buffer));
}

void txval(int val)
{
    sprintf(buffer, " %2.2x %2.2x %2.2x %2.2x",
        val&255, (val >> 8) & 255, (val >> 16) & 255, (val >> 24) & 255);
    tx((uint8_t *)buffer, strlen(buffer));
}

void txstring(unsigned char *bytes, int len)
{
    while (len-- > 0) {
        txbyte(*bytes++);
    }
}

int compute_checksum(int *ptr, int num)
{
    int checksum = 0;

    while (num-- > 0)
        checksum += *ptr++;

    return checksum;
}

/*
 * ultimately the final image to be loaded ends up in a binary blob pointed to
 * by g_filedata, of size g_filesize. g_fileptr is used to stream the data
 */

uint8_t *g_filedata;
int g_filesize;
int g_fileptr;

/*
 * read an ELF file into memory
 * optionally prepends a block of data
 * to the file (if prepend_data is non-NULL)
 */
static int
readElfFile(FILE *infile, ElfHdr *hdr, uint8_t *prepend_data, int prepend_size)
{
    ElfContext *c;
    ElfProgramHdr program;
    int size = 0;
    unsigned int base = -1;
    unsigned int top = 0;
    int i, r;
    uint8_t *program_mem;
    
    c = OpenElfFile(infile, hdr);
    if (!c) {
        printf("error: opening elf file\n");
        return -1;
    }
    /* walk through the program table */
    for (i = 0; i < c->hdr.phnum; i++) {
        if (!LoadProgramTableEntry(c, i, &program)) {
            printf("Error reading ELF program header %d\n", i);
            return -1;
        }
        if (program.type != PT_LOAD) {
            continue;
        }
        //printf("load %d bytes at %x\n", program.filesz, program.paddr);
        if (program.paddr < base) {
            base = program.paddr;
        }
        if (program.memsz < program.filesz) {
            printf("bad ELF file: program size in file too big\n");
            return -1;
        }
        if (program.paddr + program.memsz > top) {
            top = program.paddr + program.memsz;
        }
    }
    size = top - size;
    if (size > 0xffffff) {
        printf("image size %d bytes is too large to handle\n", size);
        return -1;
    }
    size += prepend_size;
    g_filedata = (uint8_t *)calloc(1, size);
    if (!g_filedata) {
        printf("Could not allocate %d bytes\n", size);
        return -1;
    }
    g_filesize = size;
    if (prepend_data && prepend_size > 0) {
        program_mem = g_filedata + prepend_size;
        memcpy(g_filedata, prepend_data, prepend_size);
    } else {
        program_mem = g_filedata;
    }
    
    for (i = 0; i < c->hdr.phnum; i++) {
        if (!LoadProgramTableEntry(c, i, &program)) {
            printf("Error reading ELF program header %d\n", i);
            return -1;
        }
        if (program.type != PT_LOAD) {
            continue;
        }
        fseek(infile, program.offset, SEEK_SET);
        r = fread(program_mem + program.paddr - base, 1, program.filesz, infile);
        if (r != program.filesz) {
            printf("read error in ELF file\n");
            return -1;
        }
    }
    //printf("ELF: total size = %d\n", size);
    return size;
}

static int verify_chksum(unsigned chksum); // forward declaration

//
// send address and size, and get back the device's response, which
// may be one of:
// 's' : go ahead and stream data
// 'k' : we need to send data in 1K chunks
// 'w' : wait for a while (device is busy e.g. erasing flash)
// 'e' : error
//
int
sendAddressSize(uint32_t address, uint32_t size)
{
    uint8_t resp[4];
    int r;
    if (verbose) printf("address=0x%08x size=%x\n", address, size);
    tx_raw_long(address);
    tx_raw_long(size);
    do {
        r = rx_timeout(resp, 1, 500);
        if (r != 1) {
            printf("sendAddressSize: timeout\n");
            return 't';
        }
        r = resp[0];
        if (verbose)
            printf("device response to header: `%c'\n", r);
    } while (r == 'w'); // device is requesting us to wait
    return r;
}

//
// download a block of data to the device at address 'address'
// returns bytes sent to device
//
int
downloadData(uint8_t *data, uint32_t address, uint32_t size)
{
    int num, i;
    unsigned chksum = 0;
    int sent = 0;
    int mode;

    if (size == 0) {
        if (verbose) printf("Skipping 0 size download at address 0x%08x\n", address);
        return 0;
    }
    // send header to device
    mode = sendAddressSize(address, size);
    if (mode == 'h') {
        printf("No himem kernel loaded, but address requires it\n");
        return -1;
    }
    if (mode != 's' && mode != 'k') {
        printf("Device reported unknown mode '%c'\n", mode);
        return -1;
    }
    chksum = 0;
    while (size > 0) {
        num = (size > 1024) ? 1024 : size;
        if (!num) break;
        if (verbose && mode == 'k') printf("Sending block of %d bytes\n", num);
        tx(data, num);
        for (i = 0; i < num; i++) {
            chksum += *data++;
        }
        size -= num;
        sent += num;
        if (size && mode == 'k') {
            // wait for device to signal it is ready
            // we may have to wait a long time
            uint8_t resp[4];
            int r = rx_timeout(resp, 1, 10000);
            if (r != 1) {
                printf("timeout while sending data to device\n");
                return -1;
            } else {
                mode = resp[0];
            }
            if (mode != 'k') {
                if (mode == 'e') {
                    // read the error message
                    uint8_t errmsg[256];
                    int r = rx_timeout(errmsg, 255, 2000);
                    if (r < 0) r = 0;
                    errmsg[r] = 0;
                    printf("Error from device: %s\n", errmsg);
                } else {
                    printf("Unexpected response '%c' from device\n", mode);
                }
                return -1;
            }
        }
    }
    // now verify the chksum
    verify_chksum(chksum);
    
    return sent;
}

//
// send a block of 'size' bytes from a file and send to device at address
// 'address'
//
int
loadBytesFromFileAtOffset(FILE *infile, uint32_t address, uint32_t size)
{
    uint8_t *localBuf;
    int num;
    
    // get memory
    localBuf = calloc(1, size);
    if (!localBuf) {
        printf("Unable to allocate %u bytes\n", size);
        return -1;
    }
    num = fread(localBuf, 1, size, infile);
    if (num != (int)size) {
        printf("Error reading data: expected %u bytes, got %d\n", size, num);
        return -1;
    }
    num = downloadData(localBuf, address, size);
    free(localBuf);

    return num;
}

//
// try loading the individual sections of an ELF file
// returns true if it was an ELF, even if we had trouble with it
//
static int
loadElfSections(const char *fname)
{
    ElfHdr hdr;
    ElfContext *c;
    ElfProgramHdr program;
    int i, size;
    FILE *f = fopen(fname, "rb");
    bool need_continue = false;
    
    if (!f) return -1;
    if (!ReadAndCheckElfHdr(f, &hdr)) {
        // not an ELF file
        fclose(f);
        return -1;
    }
    c = OpenElfFile(f, &hdr);
    if (!c) {
        fclose(f);
        return false;
    }
    size = 0;
    /* walk through the program table */
    for (i = 0; i < c->hdr.phnum; i++) {
        if (!LoadProgramTableEntry(c, i, &program)) {
            printf("Error reading ELF program header %d\n", i);
            continue;
        }
        if (program.type != PT_LOAD) {
            continue;
        }
        if (verbose) printf("load %d bytes at 0x%x\n", program.filesz, program.paddr);
        fseek(f, program.offset, SEEK_SET);
        if (need_continue) {
            tx_raw_byte('+');
        }
        size += loadBytesFromFileAtOffset(f, program.paddr, program.filesz);
        need_continue = true;
    }
    // done sending data
    //tx_raw_byte('-'); done by caller so they can handle ARGV
    fclose(f);
    return size;
}

/*
 * read a simple binary file into memory
 * sets g_filedata to point to the data, 
 * and g_filesize to the length
 * returns g_filesize, or -1 on error
 * if prepend_data is non-NULL and prepend_size > 0,
 * then that data is prepended to the total to be downloaded
 */

static int 
readBinaryFile(char *fname, uint8_t *prepend_data, int prepend_size)
{
    int size;
    int fsize;
    FILE *infile;
    ElfHdr hdr;
    uint8_t *progbase;
    
    g_fileptr = 0;
    infile = fopen(fname, "rb");
    if (!infile)
    {
        perror(fname);
        return -1;
    }
    if (ReadAndCheckElfHdr(infile, &hdr)) {
        /* this is an ELF file, load using ReadElf instead */
        return readElfFile(infile, &hdr, prepend_data, prepend_size);
    } else {
        //printf("not an ELF file\n");
    }
    
    fseek(infile, 0, SEEK_END);
    fsize = ftell(infile);
    fseek(infile, 0, SEEK_SET);
    size = ROUND_UP(fsize + prepend_size);
    g_filedata = (uint8_t *)malloc(size);
    if (!g_filedata) {
        printf("Could not allocate %d bytes\n", size);
        return -1;
    }
    if (prepend_data && prepend_size) {
        memcpy(g_filedata, prepend_data, prepend_size);
    }
    progbase = g_filedata + prepend_size;
    int origFsize = fsize;
    fsize = fread(progbase, 1, fsize, infile);
    fclose(infile);
    if (fsize <= 0) {
        size = g_filesize = fsize;
    } else {
        if (fsize != origFsize) {
            printf("WARNING: short read of file\n");
        }
        size = g_filesize = ROUND_UP(fsize + prepend_size);
    }
    return size;
}

int
loadBytesFromGBuf(char *buffer, int size)
{
    int r = 0;
    if (!g_filedata) {
        printf("No file data present\n");
        return 0;
    }
    while (size > 0 && g_fileptr < g_filesize) {
        *buffer++ = g_filedata[g_fileptr++];
        --size;
        ++r;
    }
    return r;
}

static void
patchBinaryFileForFlash(int total_size, int header_len) {
    int size = total_size;
    uint32_t *lptr;
    uint32_t chksum = 0;
    if (size & 3) {
        printf("Error: flashing a file that is not a multiple of 4 bytes long\n");
        promptexit(1);
    }
    lptr = (uint32_t *)g_filedata;
    lptr[2] = 0;                    // NOP out the DEBUG flag *before* calculating checksum
    size /= 4;
    for (int i = 0; i < size; i++) {
        chksum += lptr[i];
    }
    lptr = (uint32_t *)g_filedata;  // go to the header
    lptr[1] = -chksum;              // patch in the - chksum
}

int loadfilesingle(char *fname)
{
    int num, size, i;
    int patch = patch_mode;
    int checksum = 0;

    if (load_mode == LOAD_SPI) {
        size = readBinaryFile(fname, (uint8_t *)flash_loader_bin, flash_loader_bin_len);
        // need to patch up the binary
        if (size > 0) {
            patchBinaryFileForFlash(size, flash_loader_bin_len);
        }
    } else {
        size = readBinaryFile(fname, NULL, 0);
    }
    if (size < 0) {
        return 1;
    }
    if (verbose) printf("Loading %s - %d bytes\n", fname, size);
    tx((uint8_t *)"> Prop_Hex 0 0 0 0", 18);

    while ((num=loadBytesFromGBuf(binbuffer, 128)))
    {
        if (patch)
        {
            patch = 0;
            memcpy(&binbuffer[0x14], &clock_freq, 4);
            memcpy(&binbuffer[0x18], &clock_mode, 4);
            memcpy(&binbuffer[0x1c], &user_baud, 4);
        }
        if (use_checksum)
        {
            num = (num + 3) & ~3;
            checksum += compute_checksum(ibin, num/4);
        }
        for( i = 0; i < num; i++ )
            sprintf( &buffer[i*3], " %2.2x", binbuffer[i] & 255 );
        strcat(buffer, " > ");
        tx( (uint8_t *)buffer, strlen(buffer) );
    }
    if (use_checksum)
    {
        char *ptr = (char *)&checksum;
        checksum = 0x706f7250 - checksum;
        for( i = 0; i < 4; i++ )
            sprintf( &buffer[i*3], " %2.2x", ptr[i] & 255 );
        tx( (uint8_t *)buffer, strlen(buffer) );
        tx((uint8_t *)"?", 1);
        wait_drain();
        //msleep(100+fifo_size*10*1000/loader_baud);
        //num = rx_timeout((uint8_t *)buffer, 1, 100);
        num = rx_timeout((uint8_t *)buffer, 1, 100 + fifo_size*10000/loader_baud);
        if (num >= 0) buffer[num] = 0;
        else buffer[0] = 0;
        if (strcmp(buffer, "."))
        {
            printf("%s failed to load\n", fname);
            printf("Error response was \"%s\"\n", buffer);
            promptexit(1);
        }
        if (verbose)
            printf("Checksum (0x%08x) validated\n", checksum);
    }
    else
    {
        tx((uint8_t *)"~", 1);   // Added for Prop2-v28
        wait_drain();
        msleep(fifo_size*10*1000/loader_baud);
    }

//    msleep(100);
    if (verbose) printf("%s loaded\n", fname);
    return 0;
}
static unsigned flag_bits()
{
    // bit 0: zero out HUB memory
    // bit 1: binary has been patched with correct frequency
    return force_zero | (patch_mode << 1);
}

int loadfileFPGA(char *fname, int address)
{
    int num, size;
    int patch = patch_mode;

    size = readBinaryFile(fname, NULL, 0);
    if (size < 0)
    {
        printf("Could not open %s\n", fname);
        return 1;
    }
    if (verbose) {
        printf("Loading fast loader for %s...\n", (load_mode == LOAD_FPGA) ? "fpga" : "chip");
    }
    tx((uint8_t *)"> Prop_Hex 0 0 0 0", 18);
    txstring((uint8_t *)MainLoader_fpga_bin, MainLoader_fpga_bin_len);
    // OLD FPGA loader
    txval(clock_mode);
    txval((3*clock_freq+loader_baud)/(loader_baud*2)-extra_cycles);
    txval((clock_freq+loader_baud/2)/loader_baud-extra_cycles);
    txval(size);
    txval(address);
    txval(flag_bits());
    tx((uint8_t *)"~", 1);
    msleep(200);
    if (verbose) printf("Loading %s - %d bytes\n", fname, size);
    while ((num=loadBytesFromGBuf(buffer, 1024)))
    {
        if (patch)
        {
            patch = 0;
            memcpy(&buffer[0x14], &clock_freq, 4);
            memcpy(&buffer[0x18], &clock_mode, 4);
            memcpy(&buffer[0x1c], &user_baud, 4);
        }
        tx((uint8_t *)buffer, num);
    }
    wait_drain();
    msleep(100);
    if (verbose) printf("%s loaded\n", fname);
    return 0;
}

static char *getNextFile(char *fname, char **next_p, int *address_p)
{
    int address = *address_p;
    char *next = fname;

    if (!*fname) {
        *next_p = NULL;
        return NULL;
    }
    if (*fname == '@') {
        address = strtoul(fname+1, &next, 16);
        if (*next == '=')
            next++;
    }

    // scan ahead to start of next fname
    while (*fname && *fname != ',') {
        fname++;
    }
    if (*fname) {
        *fname++ = 0;
    }
    
    *address_p = address;
    *next_p = next;
    return fname;
}

static int verify_chksum(unsigned chksum)
{
    unsigned recv_chksum = 0;
    int num;
    wait_drain();
    //msleep(1+fifo_size*10*1000/loader_baud);
    //num = rx_timeout((uint8_t *)buffer, 3, 400);
    num = rx_timeout((uint8_t *)buffer, 3, 2000 + fifo_size*10000/loader_baud);
    if (num != 3) {
        printf("ERROR: timeout waiting for checksum at end: got %d\n", num);
        printf("Try increasing the FIFO setting if not large enough for your setup\n");
        promptexit(1);
    }
    recv_chksum = (buffer[0] - '@') << 4;
    recv_chksum += (buffer[1] - '@');
    chksum &= 0xff;
    if (recv_chksum != (chksum & 0xff)) {
        printf("ERROR: bad checksum, expected %02x got %02x (chksum characters %c%c%c)\n", chksum, recv_chksum, buffer[0], buffer[1], buffer[2]);
        promptexit(1);
    }
    if (verbose) printf("chksum: %x OK\n", recv_chksum);
    return 0;
}

int loadfile(char *fname, int address)
{
    int num, size;
    int patch = patch_mode;
    unsigned chksum;
    char *next_fname = NULL;
    int send_size = 0;
    int mode;
    
    if (load_mode == LOAD_SINGLE || load_mode == LOAD_SPI) {
        if (address != 0) {
            printf("ERROR: -SINGLE and -FLASH can only load at address 0\n");
            promptexit(1);
        }
        if (mem_argv_bytes != 0) {
            printf("ERROR: ARGv is not compatible with -SINGLE and -FLASH\n");
            promptexit(1);
        }
        return loadfilesingle(fname);
    }
    if (load_mode == LOAD_FPGA) {
        if (mem_argv_bytes != 0) {
            printf("ERROR: ARGv is not compatible with LOAD_FPGAn");
            promptexit(1);
        }
        return loadfileFPGA(fname, address);
    }
    
    if (verbose) {
        printf("Loading fast loader for %s...\n", (load_mode == LOAD_FPGA) ? "fpga" : "chip");
    }
    tx((uint8_t *)"> Prop_Hex 0 0 0 0", 18);
    txstring((uint8_t *)MainLoader_chip_bin, MainLoader_chip_bin_len);
    txval(clock_mode);
    txval(flag_bits());
    txval(0); // reserved
    txval(0); // also reserved
    tx((uint8_t *)"~", 1);
    
    {
        int retry;
        // receive checksum, verify it's "@@ "
        wait_drain();
        msleep(1+fifo_size*10*1000/loader_baud); // wait for external USB fifo to drain
        flush_input();
        msleep(50); // wait for code to start up
        tx_raw_byte(0x80);
        wait_drain();
        msleep(2);
        for (retry = 0; retry < 5; retry++) {
            // send autobaud character
            tx_raw_byte(0x80);
            wait_drain();
            msleep(10);
            num = rx_timeout((uint8_t *)buffer, 3, 200);
            if (num == 3) break;
        }
        if (num != 3) {
            printf("ERROR: timeout waiting for initial checksum: got %d\n", num);
            printf("Try increasing the FIFO setting if not large enough for your setup\n");
            promptexit(1);
        }
        // every so often we get a 0 byte first before the checksum; if
        // we do, throw it away
        if (buffer[0] == 0 && buffer[2] == '@') {
            buffer[0] = buffer[2];
            rx_timeout((uint8_t *)&buffer[2], 1, 100);
        }
        if (buffer[0] != '@' || buffer[1] != '@') {
            printf("ERROR: got incorrect initial chksum: %c%c%c (%02x %02x %02x)\n", buffer[0], buffer[1], buffer[2], buffer[0], buffer[1], buffer[2]);
            promptexit(1);
        }
    }

    // if a himem helper is present, download it to $FC000 and run it
    if (himem_bin) {
        mode = sendAddressSize(0xFC000, himem_size);
        if (mode == 's') {
            chksum = 0;
            tx(himem_bin, himem_size);
            for (unsigned i = 0; i < himem_size; i++) {
                chksum += himem_bin[i];
            }
            verify_chksum(chksum);
            tx_raw_byte('!'); // tell device to execute this plugin
        } else {
            printf("Unexpected response while downloading himem helper; ignoring\n");
        }
    }
    // we want to be able to insert 0 characters in fname
    // in order to break up multiple file names into different strings
    // so we have to copy it to a duplicate buffer
    fname = duplicate_string(fname);
    
    do {
        fname = getNextFile(fname, &next_fname, &address);
        if (!next_fname) {
            break; /* no more files */
        }
        if (*next_fname == '+') {
            next_fname++;
            send_size = 1;
        } else if (address == 0 && (send_size = loadElfSections(next_fname)) >= 0) {
            // ELF files loaded at 0 get loaded differently,
            // we can map the sections directly into HUB RAM
            // or even copy some parts to external memory
            if (mem_argv_bytes || *fname) {
                tx_raw_byte('+');
            } else {
                tx_raw_byte('-');
            }
            if (verbose) printf("Loaded %d bytes from ELF file %s\n", send_size, next_fname);
            continue;
        } else {
            send_size = 0;
        }
        if (send_size) {
            // prepend the 4 byte size to the data
            size = readBinaryFile(next_fname, (uint8_t*)&size, 4);
        } else {
            size = readBinaryFile(next_fname, NULL, 0);
        }
        if (size < 0)
        {
            printf("Could not open %s\n", next_fname);
            return 1;
        }

        /* patch the file data if necessary */
        if (patch) {
            uint8_t *buffer = (send_size) ? g_filedata+4 : g_filedata;
            patch = 0;
            if (g_filesize >= 0x24) {
                memcpy(&buffer[0x14], &clock_freq, 4);
                memcpy(&buffer[0x18], &clock_mode, 4);
                memcpy(&buffer[0x1c], &user_baud, 4);
            }
        }
        /* now send the file data from g_filedata */
        if (verbose) printf("Loading %s - %d bytes\n", next_fname, size);
        num = downloadData(g_filedata, address, g_filesize);

        if (num < 0) {
            printf("Error downloading %s\n", next_fname);
            promptexit(1);
        }
        if (mem_argv_bytes || *fname) {
            // more files to send
            tx_raw_byte('+');
        } else {
            tx_raw_byte('-');
        }
        wait_drain();
    } while (*fname);

    if (mem_argv_bytes) {
        /* send ARGv info to $FC000 */
        if (verbose) printf("sending %d arg bytes\n", mem_argv_bytes);
        tx_raw_long(ARGV_ADDR);
        tx_raw_long(mem_argv_bytes);
        chksum = 0;
        for (int i = 0; i < mem_argv_bytes; i++) {
            chksum += mem_argv_data[i];
        }
        tx((uint8_t *)mem_argv_data, mem_argv_bytes);
        verify_chksum(chksum);
        mem_argv_bytes = 0;
        tx_raw_byte('-'); // all done
        wait_drain();
    }

    msleep(100);
    return 0;
}

// check for a p2 on a specific port

static int
checkp2_and_init(char *Port, int baudrate, int retries)
{
    char buffer[101];
    int num;
    int i;
    
    if (!serial_init(Port, baudrate)) {
        return 0;
    }

    if (!do_hwreset) {
        return 1;
    }

    // reset and look for a P2
    hwreset();
    msleep(20); // wait for P2 to become active
    if (verbose) printf("trying %s...\n", Port);

    for (i = 0; i < retries; i++) {
        flush_input();
        tx((uint8_t *)"> Prop_Chk 0 0 0 0  ", 20);
        wait_drain();
        msleep(50+20*10*1000/loader_baud); // wait at least for 20 chars to empty through any fifo at the loader baud rate 
        num = rx_timeout((uint8_t *)buffer, 20, 10+20*10*1000/loader_baud); // read 20 characters 
        if (num >= 0) buffer[num] = 0;
        else {
            buffer[0] = 0;
            if (0 && verbose) printf("  timeout\n");
        }
        if (!strncmp(buffer, "\r\nProp_Ver ", 11))
        {
            if (verbose) printf("P2 version %c found on serial port %s\n", buffer[11], Port);
            if (load_mode == -1)
            {
                if (buffer[11] == 'B')
                {
                    load_mode = LOAD_FPGA;
                    if (verbose) printf("Setting load mode to FPGA\n");
                }
                else if (buffer[11] == 'A' || buffer[11] == 'G')
                {
                    load_mode = LOAD_CHIP;
                    if (verbose) printf("Setting load mode to CHIP\n");
                }
                else
                {
                    printf("Warning: Unknown version %c, assuming CHIP\n", buffer[11]);
                    load_mode = LOAD_CHIP;
                }
            }
            return 1;
        }
    }
    // if we get here we failed to find a chip
    serial_done();
    return 0;
}
    
// look for a p2
int findp2(char *portprefix, int baudrate)
{
    char Port[1024];
#ifdef INTEGER_PREFIXES    
    int i;
    char targetPath[1024];
    
    if (verbose) printf("Searching serial ports for a P2\n");
    for (i = 1; i < 255; i++)
    {
        sprintf(Port, "%s%d", portprefix, i);
        if (0==QueryDosDevice(Port, targetPath, sizeof(targetPath)))
            continue;
        if (checkp2_and_init(Port, baudrate, 50))
        {
            return 1;
        }
    }
#else
    DIR *dir;
    struct dirent *entry;
    size_t prefixlen = strlen(portprefix);

    dir = opendir("/dev");
    if (!dir) {
        printf("Unable to access /dev\n");
        promptexit(1);
    }
    for(;;) {
        entry = readdir(dir);
        if (!entry) break;
        if (0 != strncmp(entry->d_name, portprefix, prefixlen)) {
            continue;
        }
        strcpy(Port, "/dev/");
        strcat(Port, entry->d_name);
        if (checkp2_and_init(Port, baudrate, 60)) {
            closedir(dir);
            return 1;
        }
    }
    closedir(dir);
#endif
    return 0;
}

int atox(char *ptr)
{
    int value;
    sscanf(ptr, "%x", &value);
    return value;
}

int get_clock_mode(int sysfreq)
{
    int xtalfreq = 20000000;
    int xdiv = 4;
    int xdivp = 2;
    int xosc = 2;
    int xmul, xpppp, setfreq;
    //int xsel = 3;
    //int enafreq;

    if (sysfreq > 180)
    {
        xdiv = 10;
        xdivp = 1;
    }

    xmul = sysfreq/100*xdiv*xdivp/(xtalfreq/100);

    xpppp = ((xdivp >> 1) + 15) & 0xf;
    setfreq = (1 << 24) + ((xdiv-1) << 18) + ((xmul - 1) << 8) + (xpppp << 4) + (xosc << 2);
    //enafreq = setfreq + xsel;

    //printf("SYSFREQ = %d, XMUL = %d, SETFREQ = %8.8x, ENAFREQ = %8.8x\n", sysfreq, xmul, setfreq, enafreq);
    //printf("VCOFREQ = %d\n", xtalfreq/xdiv*xmul);

    return setfreq;
}

int main(int argc, char **argv)
{
    int i;
    int runterm = 0;
    int pstmode = 0;
    char *fname = 0;
    char *port = 0;
    int address = 0;
    char *u9root = 0;
    
    // Parse the command-line parameters
    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            if (argv[i][1] == 'p')
            {
                if(argv[i][2])
                    port = &argv[i][2];
                else if (++i < argc)
                    port = argv[i];
                else {
                    Usage("Missing parameter for -p");
                }
            }
            else if (argv[i][1] == 'a' || !strcmp(argv[i], "--args"))
            {
                i++;
                if (i >= argc) {
                    // allow for 0 arguments to be explicitly passed
                    argc++;
                }
                break;
            }
            else if (argv[i][1] == 'b')
            {
                if(argv[i][2])
                    user_baud = atoi(&argv[i][2]);
                else if (++i < argc)
                    user_baud = atoi(argv[i]);
                else {
                    Usage("Missing parameter for -b");
                }
            }
            else if (argv[i][1] == 'l')
            {
                if(argv[i][2])
                    loader_baud = atoi(&argv[i][2]);
                else if (++i < argc)
                    loader_baud = atoi(argv[i]);
                else 
                    Usage("Missing parameter for -l");
            }
            else if (argv[i][1] == 'X')
            {
                if(argv[i][2])
                    extra_cycles = atoi(&argv[i][2]);
                else if (++i < argc)
                    extra_cycles = atoi(argv[i]);
                else
                    Usage("Missing parameter for -X");
            }
            else if (argv[i][1] == 'e')
            {
                if(argv[i][2])
                    send_script = &argv[i][2];
                else if (++i < argc)
                    send_script = argv[i];
                else
                    Usage("Missing script for -e");
            }
            else if (argv[i][1] == 'f')
            {
                if(argv[i][2])
                    clock_freq = atoi(&argv[i][2]);
                else if (++i < argc)
                    clock_freq = atoi(argv[i]);
                else
                    Usage("Missing frequency for -f");
            }
            else if (!strcmp(argv[i], "-FIFO"))
            {
                if (++i < argc)
                    fifo_size = atoi(argv[i]);
                else
                    Usage("Missing byte count for -FIFO");
            }
            else if (argv[i][1] == 'k')
            {
                waitAtExit = 1;
            }
            else if (argv[i][1] == 'q')
            {
                quiet_mode = 1;
            }
            else if (argv[i][1] == 'n')
            {
                do_hwreset = 0;
            }
            else if (argv[i][1] == 'm')
            {
                if(argv[i][2])
                    clock_mode = atox(&argv[i][2]);
                else if (++i < argc)
                    clock_mode = atox(argv[i]);
                else
                    Usage("Missing clock mode for -m");
            }
            else if (argv[i][1] == 's')
            {
                if(argv[i][2])
                    address = atox(&argv[i][2]);
                else if (++i < argc)
                    address = atox(argv[i]);
                else
                    Usage("Missing start address for -s");
            }
            else if (argv[i][1] == 't')
                runterm = 1;
            else if (argv[i][1] == 'T') {
                runterm = pstmode = 1;
            }
            else if (argv[i][1] == 'x') {
                char *monitor = NULL;
                if (argv[i][2])
                    monitor = &argv[i][2];
                else if (++i < argc)
                    monitor = &argv[i][0];
                else
                    Usage("Missing option for -x");
                if (monitor) {
                    if (!strcmp(monitor, "TAQOZ")) {
                        enter_rom = ENTER_TAQOZ;
                    } else if (!strcmp(monitor, "DEBUG")) {
                        enter_rom = ENTER_DEBUG;
                    } else if (!strcmp(monitor, "TERM")) {
                        do_hwreset = 0;
                        runterm = 1;
                    } else {
                        Usage("Unknown monitor option after -x");
                    }
                } else {
                    Usage("Missing monitor after -x");
                }
            }
            else if (argv[i][1] == 'v')
                verbose = 1;
            else if (argv[i][1] == '?')
            {
                Usage(NULL);
            }
            else if (argv[i][1] == '9')
            {
                if(argv[i][2])
                    u9root = &argv[i][2];
                else if (++i < argc)
                    u9root = &argv[i][0];
                else
                    Usage("Missing directory option for -9");
            }
            else if (!strcmp(argv[i], "-PATCH"))
                patch_mode = 1;
            else if (!strcmp(argv[i], "-CHIP"))
                load_mode = LOAD_CHIP;
            else if (!strcmp(argv[i], "-FPGA"))
                load_mode = LOAD_FPGA;
            else if (!strcmp(argv[i], "-SINGLE"))
                load_mode = LOAD_SINGLE;
            else if (!strcmp(argv[i], "-SPI") || !strcmp(argv[i], "-FLASH") ) {
                load_mode = LOAD_SPI;
                use_checksum = 0; /* checksum calculation throws off flash loader */
            } else if (!strncmp(argv[i], "-HIMEM=", 7)) {
                char *himem_kind = argv[i] + 7;
                if (!strcmp(himem_kind, "flash")) {
                    himem_bin = (uint8_t *)himem_flash_bin;
                    himem_size = himem_flash_bin_len;
                } else {
                    printf("Unknown HIMEM memory type %s\n", himem_kind);
                    Usage(NULL);
                }
            } else if (!strcmp(argv[i], "-NOZERO")) {
                force_zero = 0;
            } else if (!strcmp(argv[i], "-ZERO"))
                force_zero = 1;
            else if (!strcmp(argv[i], "-DTR"))
                serial_use_rts_for_reset(0);
            else if (!strcmp(argv[i], "-RTS"))
                serial_use_rts_for_reset(1);
            else if (!strcmp(argv[i], "-NOEOF"))
                ignoreEof = 1;
            else
            {
                printf("Invalid option %s\n", argv[i]);
                Usage(NULL);
            }
        }
        else
        {
            if (fname) Usage("too many files specified on command line");
            fname = argv[i];
        }
    }

    if (i < argc) {
        int num_argc = 1;
        mem_argv_bytes = 5; // for ARGv plus trailing 0
        // find length of all arguments
        for (int j = i; j < argc && argv[j]; j++) {
            mem_argv_bytes += strlen(argv[j]) + 1; // include trailing 0
            num_argc++;
        }
        if (mem_argv_bytes >= ARGV_MAX_BYTES) {
            printf("Argument list too long (%d bytes, maximum is %d)\n",
                   mem_argv_bytes, ARGV_MAX_BYTES);
            promptexit(1);
        }
        if (num_argc >= ARGC_MAX_ITEMS) {
            printf("Too many arguments (%d provided, maximum is %d)\n",
                   num_argc, ARGC_MAX_ITEMS);
            promptexit(1);
        }
        mem_argv_data = (char *)calloc(1, mem_argv_bytes);
        char *ptr = mem_argv_data;
        *ptr++ = 'A';
        *ptr++ = 'R';
        *ptr++ = 'G';
        *ptr++ = 'v';
        for (int j = i; j < argc && argv[j]; j++) {
            strcpy(ptr, argv[j]);
            ptr += strlen(argv[j]) + 1;
        }
        // do not have to 0 terminate, we used calloc above
    }
    if (enter_rom) {
        if (fname) {
            printf("Entering ROM is incompatible with downloading a file\n");
            Usage(NULL);
        }
    }
    if (!fname && !runterm && !enter_rom) {
        Usage("Must specify a file name or -t or -x");
    }
    // Determine the user baud rate
    if (user_baud == -1)
    {
        user_baud = clock_freq / 10 * 9 / 625;
        if (verbose) printf("Setting user_baud to %d\n", user_baud);
    }

    // Initialize the loader baud rate
    // on some platforms the user and loader baud rates must match
    // this does not matter if we are not starting a terminal
    if (runterm || enter_rom || send_script)
    {
        int new_loader_baud = get_loader_baud(user_baud, loader_baud);
        if (new_loader_baud != loader_baud) {
            printf("Platform required loader baud to be changed to %d\n", new_loader_baud);
            loader_baud = new_loader_baud;
        }
        if (!fname) {
            loader_baud = user_baud;
        }
    }
    
    // Determine the P2 serial port
    if (!port)
    {
        if (!findp2(PORT_PREFIX, loader_baud))
        {
            printf("Could not find a P2\n");
            promptexit(1);
        }
    }
    else
    {
        if (!checkp2_and_init(port, loader_baud, 100))
        {
            printf("Could not find a P2 on port %s\n", port);
            promptexit(1);
        }
    }
    if (fname)
    {
        if (load_mode == LOAD_CHIP)
        {
            if (clock_mode == -1)
            {
                clock_mode = get_clock_mode(clock_freq);
                if (verbose) printf("Setting clock_mode to %x\n", clock_mode);
            }
        }
        else if (load_mode == LOAD_FPGA)
        {
            int temp = clock_freq / 312500; // * 256 / 80000000
            int temp1 = temp - 1;
            if (clock_mode == -1)
            {
                clock_mode = temp1;
                if (verbose) printf("Setting clock_mode to %x\n", temp1);
            }
        }
        else if (load_mode == -1)
        {
            load_mode = LOAD_SINGLE;
            if (verbose) printf("Setting load mode to SINGLE\n");
        }

        if (loadfile(fname, address))
        {
            serial_done();
            promptexit(1);
        }
    }

    if (u9root) {
        runterm = 3;
        u9fs_init(u9root);
    }
    if (runterm || enter_rom || send_script)
    {
        serial_baud(user_baud);
        switch(enter_rom) {
        case ENTER_DEBUG:
            tx((uint8_t *)"> \004", 3);
            break;
        case ENTER_TAQOZ:
            tx((uint8_t *)"> \033", 3);
            break;
        default:
            break;
        }
        if (send_script) {
            RunScript(send_script);
        }
        if (runterm) {
            if (!quiet_mode) {
                printf("( Entering terminal mode.  Press Ctrl-] or Ctrl-Z to exit. )\n");
            }
            terminal_mode(runterm, pstmode);
            if (!quiet_mode) {
                waitAtExit = 0; // no need to wait, user explicitly quit
            }
        }
    }

    serial_done();
    promptexit(0);
}

//
// script file helper routines
//

// insert a 1 ms pause after this many characters are typed (0 disables)
static int scriptVarPauseAfter = 0;

// default timeout in milliseconds for recv() function (0 disables)
static int scriptVarRecvTimeout = 2000;

// some setter functions
static int scriptRecvtimeout(char *arg)
{
    int val = atoi(arg);
    if (val == 0) {
        if (!isdigit(*arg)) {
            printf("bad parameter to recvtimeout\n");
            return 0;
        }
    }
    scriptVarRecvTimeout = val;
    return 1;
}

// some setter functions
static int scriptPauseafter(char *arg)
{
    int val = atoi(arg);
    if (val == 0) {
        if (!isdigit(*arg)) {
            printf("bad parameter to pauseafter\n");
            return 0;
        }
    }
    scriptVarPauseAfter = val;
    return 1;
}

// send contents of a file:
// if binary, send contents verbatim
// if !binary, translate \n -> \r and drop \r

static int SendFile(char *filename, int binary)
{
    FILE *f;
    int c;
    int count = 0;

    f = fopen(filename, binary ? "rb" : "rt");    
    if (!f) {
        perror(filename);
        return 0;
    }
    for(;;) {
        c = fgetc(f);
        if (c < 0) break;
        if (binary) {
            tx_raw_byte(c);
        } else if (c == '\r') {
            // skip CR
        } else if (c == '\n') {
            tx_raw_byte('\r');
        } else {
            tx_raw_byte(c);
        }
        count++;
        if (count >= scriptVarPauseAfter) {
            // pause periodically for the other end to keep up
            msleep(1);
            count = 0;
        }
    }
    fclose(f);
    return 1;
}

static int
scriptTextfile(char *name)
{
    return SendFile(name, 0);
}

static int
scriptBinfile(char *name)
{
    return SendFile(name, 1);
}

static int scriptRecv(char *string)
{
    int num;
    char *here = string;
    unsigned long long start = elapsedms();
    unsigned long long now;
    
    for(;;) {
        now = elapsedms();
        if ( scriptVarRecvTimeout && (now - start) > scriptVarRecvTimeout ) {
            printf("ERROR: timeout waiting for string [%s]\n", string);
            return 0;
        }
        num = rx_timeout((uint8_t *)buffer, 1, 1);
        if ((num <= 0)) {
            continue;
        }
        if (buffer[0] != *here) {
            // reset our expectations
            here = string;
            continue;
        }
        here++;
        if (!*here) {
            break;
        }
    }
    return 1;
}

static int scriptSend(char *string)
{
    int count = 0;
    int c;

    while ( (c = *string++) != 0 ) {
        tx_raw_byte(c);
        count++;
        if ( scriptVarPauseAfter && count >= scriptVarPauseAfter) {
            msleep(1);
            count = 0;
        }
    }
    return 1;
}

static int scriptPausems(char *arg)
{
    int delay = atoi(arg);
    if (delay > 0) {
        msleep(delay);
    }
    return 1;
}

#define MAX_SCRIPTFILE_SIZE (256*1024)

static int scriptScriptfile(char *arg)
{
    char *origscript;
    FILE *f = fopen(arg, "r");
    int r;
    
    if (!f) {
        perror(arg);
        return 0;
    }
    origscript = calloc(1, MAX_SCRIPTFILE_SIZE);
    if (!origscript) {
        printf("Out of memory in scriptfile\n");
        fclose(f);
        return 0;
    }
    r = fread(origscript, 1, MAX_SCRIPTFILE_SIZE, f);
    fclose(f);
    if (r <= 0) {
        printf("Read error in script `%s'\n", arg);
        return 0;
    }
    if (r >= MAX_SCRIPTFILE_SIZE-1) {
        printf("Script file `%s' is too large\n", arg);
        return 0;
    }
    RunScript(origscript);
    free(origscript);
    return 1;
}

// script commands
typedef struct command {
    const char *name;
    int (*func)(char *arg);
} Command;

static Command cmdlist[] = {
    { "binfile", scriptBinfile },
    { "pauseafter", scriptPauseafter },
    { "pausems", scriptPausems },
    { "recv", scriptRecv },
    { "recvtimeout", scriptRecvtimeout },
    { "scriptfile", scriptScriptfile },
    { "send", scriptSend },
    { "textfile", scriptTextfile },
    { 0, 0 }
};

static int scriptVarStringStart;

// fetch the next command, and advance the script
// pointer to just after it
Command *GetCmd(char **script_p)
{
    char *script = *script_p;
    char *cmdstr = NULL;
    Command *cmd = NULL;
    int c;

    scriptVarStringStart = 0;
    while ( !cmd && (c = *script) != 0) {
        cmdstr = script++;
        if (isspace(c)) {
            // just skip spaces
            continue;
        }
        if (c == '#') {
            // comment; skip to end of line
            while ( (c = *script) != 0 ) {
                script++;
                if (c == '\n') break;
            }
            continue;
        }
        if (!isalpha(c)) {
            printf("Unexpected character `%c' in script (searching for command name)\n", c);
            break;
        }
        //
        // this is a command
        // 
        while (isalpha(*script)) script++;
        if (*script) {
            scriptVarStringStart=*script;
            *script++ = 0;
        }
        // look up the command
        for (cmd = &cmdlist[0]; cmd->name; cmd++) {
            if (!strcmp(cmd->name, cmdstr)) {
                break;
            }
        }
        if (!cmd->name) {
            printf("ERROR: unknown command `%s' in script\n", cmdstr);
            cmd = NULL;
        }
    }
    *script_p = script;
    return cmd;
}

// read a string terminated by the character 'term'
// returns a pointer to the string, and updates *ptr to point to the end
// also translates any escape sequences within the string
// it does the translation in place, since any ^ sequence translates
// to something shorter than itself
static char *GetString(int term, char **where_p)
{
    char *script = *where_p;
    char *argname;
    char *dst;
    int c;

    c = *script;
    if (!c || c==term) {
        printf("ERROR: script expected string terminated with %c\n", term);
        return NULL;
    }
    argname = dst = script;
    while ( (c = *script) != 0 && c != term) {
        script++;
        if (c == '^') {
            // translate destination
            c = *script;
            if (!c) break;
            script++;
            if (c == '^' || c == term) {
                *dst++ = c;
            } else if (isalpha(c) || c == '[' || c == '@') {
                *dst++ = (c & 0x1f);
            } else if (isdigit(c)) {
                c = (c - '0');
                while (*script && isdigit(*script)) {
                    c = 10*c + (*script - '0');
                    script++;
                }
                *dst++ = c;
            } else {
                printf("Unknown ^ escape character `%c' in script\n", c);
            }
        } else {
            *dst++ = c;
        }
    }
    if (*script) {
        script++;
    }
    *dst = 0;
    *where_p = script;
    return argname;
}

static void RunScript(char *script)
{
    Command *cmd;
    char *arg;
    int c;
    int r;
    
    for(;;) {
        //printf("script=[%s]\n", script);
        cmd = GetCmd(&script);
        if (!cmd) break;
        //printf("Got command [%s] script=[%s]\n", cmd->name, script);
        if (scriptVarStringStart) {
            c = scriptVarStringStart;
        } else {
            c = 0;
        }
        while (c && isspace(c)) {
            c = *script;
            if (c) script++;
        }
        if (!c) break;
        if (c == '(') {
            arg = GetString(')', &script);
        } else if (c == '[') {
            arg = GetString(']', &script);
        } else if (c == '{') {
            arg = GetString('}', &script);
        } else {
            printf("Unexpected character `%c' in script (after %s)\n", c, cmd->name);
            arg = NULL;
        }
        if (!arg) break;
        //printf("Command=%s arg=[%s]\n", cmd->name, arg);
        r = (*cmd->func)(arg);
        if (!r) break;
    }
}
