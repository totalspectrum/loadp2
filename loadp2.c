/*
 *
 * Copyright (c) 2017-2019 by Dave Hein
 * Copyright (c) 2019 Total Spectrum Software Inc.
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
#include "osint.h"
#include "loadelf.h"

#define LOAD_CHIP   0
#define LOAD_FPGA   1
#define LOAD_SINGLE 2

#if defined(__APPLE__)
static int loader_baud = 921600;
#else
static int loader_baud = 2000000;
#endif
static int clock_mode = -1;
static int user_baud = 115200;
static int clock_freq = 80000000;
static int extra_cycles = 7;
static int load_mode = -1;
static int patch_mode = 0;
static int use_checksum = 1;
static int quiet_mode = 0;
static int need_reset = 1;

int get_loader_baud(int ubaud, int lbaud);

#if defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__)
  #define PORT_PREFIX "com"
#elif defined(__APPLE__)
  #define PORT_PREFIX "/dev/cu.usbserial"
#else
  #define PORT_PREFIX "/dev/ttyUSB"
#endif

#include "MainLoader.h"
#include "MainLoader1.h"

static int32_t ibuf[256];
static int32_t ibin[32];
static char *buffer = (char *)ibuf;
static char *binbuffer = (char *)ibin;
static int verbose = 0;
static int waitAtExit = 0;
static int force_zero = 1;  /* default to zeroing memory */

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
static void Usage(void)
{
printf("\
loadp2 - a loader for the propeller 2 - version 0.025 2019-10-20\n\
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
         [ -q ]                    quiet mode: also checks for magic escape sequence\n\
         [ -? ]                    display a usage message and exit\n\
         [ -CHIP ]                 set load mode for CHIP\n\
         [ -FPGA ]                 set load mode for FPGA\n\
         [ -SINGLE ]               set load mode for single stage\n\
         [ -PATCH ]                patch in clock frequency and serial parms\n\
         [ -NOZERO ]               do not clear memory before download\n\
         file                      file to load\n", user_baud, loader_baud, clock_freq, clock_mode);
    promptexit(1);
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
 */
int
loadElfFile(FILE *infile, ElfHdr *hdr)
{
    ElfContext *c;
    ElfProgramHdr program;
    int size = 0;
    unsigned int base = -1;
    unsigned int top = 0;
    int i, r;
    
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
    g_filedata = (uint8_t *)calloc(1, size);
    if (!g_filedata) {
        printf("Could not allocate %d bytes\n", size);
        return -1;
    }
    g_filesize = size;
    for (i = 0; i < c->hdr.phnum; i++) {
        if (!LoadProgramTableEntry(c, i, &program)) {
            printf("Error reading ELF program header %d\n", i);
            return -1;
        }
        if (program.type != PT_LOAD) {
            continue;
        }
        fseek(infile, program.offset, SEEK_SET);
        r = fread(g_filedata + program.paddr - base, 1, program.filesz, infile);
        if (r != program.filesz) {
            printf("read error in ELF file\n");
            return -1;
        }
    }
    //printf("ELF: total size = %d\n", size);
    return size;
}

/*
 * read a simple binary file into memory
 * sets g_filedata to point to the data, 
 * and g_filesize to the length
 * returns g_filesize, or -1 on error
 */

int 
loadBinaryFile(char *fname)
{
    int size;
    FILE *infile;
    ElfHdr hdr;
    
    g_fileptr = 0;
    infile = fopen(fname, "rb");
    if (!infile)
    {
        perror(fname);
        return -1;
    }
    if (ReadAndCheckElfHdr(infile, &hdr)) {
        /* this is an ELF file, load using LoadElf instead */
        return loadElfFile(infile, &hdr);
    } else {
        //printf("not an ELF file\n");
    }
    
    fseek(infile, 0, SEEK_END);
    size = ftell(infile);
    fseek(infile, 0, SEEK_SET);
    g_filedata = (uint8_t *)malloc(size);
    if (!g_filedata) {
        printf("Could not allocate %d bytes\n", size);
        return -1;
    }
    size = g_filesize = fread(g_filedata, 1, size, infile);
    fclose(infile);
    return size;
}

int
loadBytes(char *buffer, int size)
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

int loadfilesingle(char *fname)
{
    int num, size, i;
    int patch = patch_mode;
    int totnum = 0;
    int checksum = 0;

    size = loadBinaryFile(fname);
    if (size < 0) {
        return 1;
    }
    if (verbose) printf("Loading %s - %d bytes\n", fname, size);
    if (need_reset) {
        hwreset();
        msleep(50);
    }
    tx((uint8_t *)"> Prop_Hex 0 0 0 0", 18);

    while ((num=loadBytes(binbuffer, 128)))
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
        totnum += num;
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
        num = rx_timeout((uint8_t *)buffer, 1, 100);
        if (num >= 0) buffer[num] = 0;
        else buffer[0] = 0;
        if (strcmp(buffer, "."))
        {
            printf("%s failed to load\n", fname);
            printf("Error response was \"%s\"\n", buffer);
            promptexit(1);
        }
        if (verbose)
            printf("Checksum validated\n");
    }
    else
    {
        tx((uint8_t *)"~", 1);   // Added for Prop2-v28
        wait_drain();
    }

    msleep(100);
    if (verbose) printf("%s loaded\n", fname);
    return 0;
}

static unsigned flag_bits()
{
    // bit 0: zero out HUB memory
    // bit 1: binary has been patched with correct frequency
    return force_zero | (patch_mode << 1);
}

int loadfile(char *fname, int address)
{
    int num, size;
    int totnum = 0;
    int patch = patch_mode;
    unsigned chksum = 0;
    
    if (load_mode == LOAD_SINGLE)
        return loadfilesingle(fname);

    size = loadBinaryFile(fname);
    if (size < 0)
    {
        printf("Could not open %s\n", fname);
        return 1;
    }
    if (need_reset) {
        hwreset();
        msleep(50);
    }
    if (verbose) {
        printf("Loading fast loader for %s...\n", (load_mode == LOAD_FPGA) ? "fpga" : "chip");
    }
    tx((uint8_t *)"> Prop_Hex 0 0 0 0", 18);
    if (load_mode == LOAD_FPGA) {
        txstring((uint8_t *)MainLoader_bin, MainLoader_bin_len);
    } else {
        txstring((uint8_t *)MainLoader1_bin, MainLoader1_bin_len);
    }
    txval(clock_mode);
    if (load_mode == LOAD_CHIP) {
        double clocks_per_bit = (double)clock_freq / (double)loader_baud;
        unsigned long bit_period = 0x10000 * clocks_per_bit;
        unsigned long bit15_period = 0x10000 * (1.5 * clocks_per_bit);
        bit_period &= ~0x3FF;
        bit15_period &= ~0x3FF;
        txval(bit15_period);
        txval(bit_period);
    } else {
        txval((3*clock_freq+loader_baud)/(loader_baud*2)-extra_cycles);
        txval((clock_freq+loader_baud/2)/loader_baud-extra_cycles);
    }
    txval(size);
    txval(address);
    txval(flag_bits());
    tx((uint8_t *)"~", 1);
    if (load_mode == LOAD_FPGA) {
        msleep(200);
    } else {
        // receive checksum, verify it's "@@ "
        msleep(200);
        num = rx_timeout((uint8_t *)buffer, 3, 400);
        if (num != 3) {
            printf("ERROR: timeout waiting for initial checksum: got %d\n", num);
            promptexit(1);
        }
        if (buffer[0] != '@' || buffer[1] != '@') {
            printf("ERROR: got incorrect initial chksum: %c%c%c\n", buffer[0], buffer[1], buffer[2]);
            promptexit(1);
        }
    }
    if (verbose) printf("Loading %s - %d bytes\n", fname, size);
    while ((num=loadBytes(buffer, 1024)))
    {
        int i;
        if (patch)
        {
            patch = 0;
            memcpy(&buffer[0x14], &clock_freq, 4);
            memcpy(&buffer[0x18], &clock_mode, 4);
            memcpy(&buffer[0x1c], &user_baud, 4);
        }
        tx((uint8_t *)buffer, num);
        totnum += num;
        for (i = 0; i < num; i++) {
            chksum += buffer[i];
        }
    }
    // receive checksum, verify it
    if (load_mode == LOAD_CHIP) {
        int recv_chksum = 0;
        wait_drain();
        num = rx_timeout((uint8_t *)buffer, 3, 400);
        if (num != 3) {
            printf("ERROR: timeout waiting for checksum at end: got %d\n", num);
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
    } else {
        wait_drain();
    }
    msleep(100);
    if (verbose) printf("%s loaded\n", fname);
    return 0;
}

// check for a p2 on a specific port
static int
checkp2_and_init(char *Port, int baudrate, int sleeptime)
{
    char buffer[101];
    int num;
    if (!serial_init(Port, baudrate)) {
        return 0;
    }

    if (verbose) printf("trying %s with timeout of %d ms...\n", Port, sleeptime);
    if (need_reset) {
        hwreset();
        msleep(sleeptime);
    }
    tx((uint8_t *)"> Prop_Chk 0 0 0 0  ", 20);
    msleep(50);
    num = rx_timeout((uint8_t *)buffer, 100, 10);
    if (num >= 0) buffer[num] = 0;
    else {
        buffer[0] = 0;
        if (verbose) printf("  timeout\n");
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
        return sleeptime;
    }
    serial_done();
    return 0;
}

// look for a p2
// returns delay to use for it after reset
int findp2(char *portprefix, int baudrate)
{
    int i;
    int sleeptime;
    
    char Port[100];

    if (verbose) printf("Searching serial ports for a P2\n");
    for (sleeptime = 50; sleeptime < 1000; sleeptime += 450) {
        for (i = 0; i < 20; i++)
        {
            sprintf(Port, "%s%d", portprefix, i);
            if (checkp2_and_init(Port, baudrate, sleeptime))
            {
                need_reset = 0;
                return sleeptime;
            }
        }
        // no port found yet? sleep a while and try again
        need_reset = 0;
        sleeptime += 400;
    }
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
                else
                    Usage();
            }
            else if (argv[i][1] == 'b')
            {
                if(argv[i][2])
                    user_baud = atoi(&argv[i][2]);
                else if (++i < argc)
                    user_baud = atoi(argv[i]);
                else
                    Usage();
            }
            else if (argv[i][1] == 'l')
            {
                if(argv[i][2])
                    loader_baud = atoi(&argv[i][2]);
                else if (++i < argc)
                    loader_baud = atoi(argv[i]);
                else
                    Usage();
            }
            else if (argv[i][1] == 'X')
            {
                if(argv[i][2])
                    extra_cycles = atoi(&argv[i][2]);
                else if (++i < argc)
                    extra_cycles = atoi(argv[i]);
                else
                    Usage();
            }
            else if (argv[i][1] == 'f')
            {
                if(argv[i][2])
                    clock_freq = atoi(&argv[i][2]);
                else if (++i < argc)
                    clock_freq = atoi(argv[i]);
                else
                    Usage();
            }
            else if (argv[i][1] == 'k')
            {
                waitAtExit = 1;
            }
            else if (argv[i][1] == 'q')
            {
                quiet_mode = 1;
            }
            else if (argv[i][1] == 'm')
            {
                if(argv[i][2])
                    clock_mode = atox(&argv[i][2]);
                else if (++i < argc)
                    clock_mode = atox(argv[i]);
                else
                    Usage();
            }
            else if (argv[i][1] == 's')
            {
                if(argv[i][2])
                    address = atox(&argv[i][2]);
                else if (++i < argc)
                    address = atox(argv[i]);
                else
                    Usage();
            }
            else if (argv[i][1] == 't')
                runterm = 1;
            else if (argv[i][1] == 'T')
                runterm = pstmode = 1;
            else if (argv[i][1] == 'v')
                verbose = 1;
            else if (!strcmp(argv[i], "-PATCH"))
                patch_mode = 1;
            else if (!strcmp(argv[i], "-CHIP"))
                load_mode = LOAD_CHIP;
            else if (!strcmp(argv[i], "-FPGA"))
                load_mode = LOAD_FPGA;
            else if (!strcmp(argv[i], "-SINGLE"))
                load_mode = LOAD_SINGLE;
            else if (!strcmp(argv[i], "-NOZERO"))
                force_zero = 0;
            else
            {
                printf("Invalid option %s\n", argv[i]);
                Usage();
            }
        }
        else
        {
            if (fname) Usage();
            fname = argv[i];
        }
    }

    if (!fname && (!port || !runterm)) Usage();

    // Determine the user baud rate
    if (user_baud == -1)
    {
        user_baud = clock_freq / 10 * 9 / 625;
        if (verbose) printf("Setting user_baud to %d\n", user_baud);
    }

    // Initialize the loader baud rate
    // on some platforms the user and loader baud rates must match
    // this does not matter if we are not starting a terminal
    if (runterm)
    {
        int new_loader_baud = get_loader_baud(user_baud, loader_baud);
        if (new_loader_baud != loader_baud) {
            printf("Platform required loader baud to be changed to %d\n", new_loader_baud);
            loader_baud = new_loader_baud;
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
        if (!checkp2_and_init(port, loader_baud, 50))
        {
            if (!checkp2_and_init(port, loader_baud, 500)) {
                printf("Could not find a P2 on port %s\n", port);
                promptexit(1);
            }
        }
        need_reset = 0;
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

    if (runterm)
    {
        serial_baud(user_baud);
        if (!quiet_mode) {
            printf("( Entering terminal mode.  Press Ctrl-] to exit. )\n");
        }
        terminal_mode(1,pstmode);
        if (!quiet_mode) {
            waitAtExit = 0; // no need to wait, user explicitly quite
        }
    }

    serial_done();
    promptexit(0);
}
