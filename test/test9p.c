//
// simple test program for 9p access to host files
//

#include <string.h>
#include <stdint.h>
#include "fs9p.h"

struct __using("spin/SmartSerial") ser;

typedef struct fsfile {
    uint32_t offlo;
    uint32_t offhi;
} fs_file;

int maxlen = MAXLEN;
static uint8_t txbuf[MAXLEN];

// command to put 1 byte in the host buffer
uint8_t *doPut1(uint8_t *ptr, unsigned x) {
    *ptr++ = x;
    return ptr;
}

// send a 16 bit integer to the host
uint8_t *doPut2(uint8_t *ptr, unsigned x) {
    ptr = doPut1(ptr, x & 0xff);
    ptr = doPut1(ptr, (x>>8) & 0xff);
    return ptr;
}
// send a 32 bit integer to the host
uint8_t *doPut4(uint8_t *ptr, unsigned x) {
    ptr = doPut1(ptr, x & 0xff);
    ptr = doPut1(ptr, (x>>8) & 0xff);
    ptr = doPut1(ptr, (x>>16) & 0xff);
    ptr = doPut1(ptr, (x>>24) & 0xff);
    return ptr;
}

uint8_t *doPutStr(uint8_t *ptr, const char *s) {
    unsigned L = strlen(s);
    unsigned i = 0;
    ptr = doPut2(ptr, L);
    for (i = 0; i < L; i++) {
        *ptr++ = *s++;
    }
    return ptr;
}

// receive 1 byte
unsigned int doGet1()
{
    int c;
    do {
        c = ser.rx();
    } while (c < 0);
    return c;
}

// receive an unsigned long
unsigned doGet4()
{
    unsigned r;
    r = doGet1();
    r = r | (doGet1() << 8);
    r = r | (doGet1() << 16);
    r = r | (doGet1() << 24);
    return r;
}

// send a buffer to the host
// then receive a reply
int sendRecv(uint8_t *startbuf, uint8_t *endbuf)
{
    int len = endbuf - startbuf;
    uint8_t *buf = startbuf;
    int i = 0;
    int left;
    
    doPut4(startbuf, len);
    if (len <= 4) {
        ser.printf("HELP!: len=%d\n", len);
        pausems(1000);
        for(;;);
    }
    ser.tx(0xff);
    ser.tx(0x01);
    while (len>0) {
        ser.tx(*buf++);
        --len;
    }
    len = doGet4() - 4;
    left = len;
    while (left > 0 && i < maxlen) {
        startbuf[i++] = doGet1();
        --left;
    }
    return len;
}

static unsigned FETCH2(uint8_t *b)
{
    unsigned r;
    r = b[0];
    r |= (b[1]<<8);
    return r;
}
static unsigned FETCH4(uint8_t *b)
{
    unsigned r;
    r = b[0];
    r |= (b[1]<<8);
    r |= (b[2]<<16);
    r |= (b[3]<<24);
    return r;
}

// root directory for connection
// set up by fs_init
fs_file rootdir;

// initialize connection to host
// returns 0 on success, -1 on failure
int fs_init()
{
    uint8_t *ptr;
    uint32_t size;
    int len;
    unsigned msize;
    unsigned s;
    unsigned tag;

    ptr = doPut4(txbuf, 0);
    ptr = doPut1(ptr, Tversion);
    ptr = doPut2(ptr, NOTAG);
    ptr = doPut4(ptr, MAXLEN);
    ptr = doPutStr(ptr, "9P2000");
    len = sendRecv(txbuf, ptr);

    ptr = txbuf;

    if (ptr[0] != Rversion) {
        ser.printf("No version response from host\n");
        ser.printf("len=%x buf[] = %x %x %x %x ",
                   len, ptr[0], ptr[1], ptr[2], ptr[3]);
        ser.printf(" %x %x %x %x\n",
                   ptr[4], ptr[5], ptr[6], ptr[7]);
        return -1;
    }
    
    tag = FETCH2(ptr+1);
    msize = FETCH4(ptr+3);

    s = FETCH2(ptr+7);
    if (s != 6 || 0 != strncmp(&ptr[9], "9P2000")) {
        ser.printf("Bad version response from host: s=%d ver=%s\n", s, &ptr[9]);
        return -1;
    }
    if (msize < 64 || msize > MAXLEN) {
        ser.printf("max message size %u is out of range\n", msize);
        return -1;
    }
    maxlen = msize;

    // OK, try to attach
    ptr = doPut4(txbuf, 0);  // space for size
    ptr = doPut1(ptr, Tattach);
    ptr = doPut2(ptr, NOTAG);  // not sure about this one...
    ptr = doPut4(ptr, (uint32_t)&rootdir); // our FID
    ptr = doPut4(ptr, NOFID); // no authorization requested
    ptr = doPutStr(ptr, "user");
    ptr = doPutStr(ptr, ""); // no aname

    len = sendRecv(txbuf, ptr);
    
    ptr = txbuf;
    if (ptr[0] != Rattach) {
        ser.printf("Unable to attach\n");
        return -1;
    }
    return 0;
}

// walk from fid "dir" along path, creating fid "newfile"
int fs_walk(fs_file *dir, fs_file *newfile, char *path)
{
    uint8_t *ptr;
    uint8_t *sizeptr;
    int c;
    uint32_t curdir = (uint32_t) dir;
    int len;
    int r;
    
    do {
        ptr = doPut4(txbuf, 0); // space for size
        ptr = doPut1(ptr, Twalk);
        ptr = doPut2(ptr, NOTAG);
        ptr = doPut4(ptr, curdir);
        curdir = (uint32_t)newfile;
        ptr = doPut4(ptr, curdir);
        while (*path == '/') path++;
        len = ptr - txbuf;
        if (*path) {
            ptr = doPut2(ptr, 1); // nwname
            sizeptr = ptr;
            ptr = doPut2(ptr, 0);
            while (*path && *path != '/' && len < maxlen) {
                *ptr++ = *path++;
                len++;
            }
            doPut2(sizeptr, (uint32_t)(ptr - (sizeptr+2)));
        } else {
            ptr = doPut2(ptr, 0);
        }

        r = sendRecv(txbuf, ptr);
        if (txbuf[0] != Rwalk) {
            return -1;
        }
    } while (*path);
    return 0;
}

fs_file testfile;

int fs_open(fs_file *f, char *path, int fs_mode)
{
    int r;
    uint8_t *ptr;
    uint8_t mode = 0;
    
    r = fs_walk(&rootdir, f, path);
    if (r != 0) return r;
    ptr = doPut4(txbuf, 0); // space for size
    ptr = doPut1(ptr, Topen);
    ptr = doPut2(ptr, NOTAG);
    ptr = doPut4(ptr, (uint32_t)f);
    ptr = doPut1(ptr, mode);
    r = sendRecv(txbuf, ptr);
    if (txbuf[0] != Ropen) {
        return -1;
    }
    f->offlo = f->offhi = 0;
    return 0;
}

int fs_close(fs_file *f)
{
    uint8_t *ptr;
    int r;
    ptr = doPut4(txbuf, 0); // space for size
    ptr = doPut1(ptr, Tclunk);
    ptr = doPut2(ptr, NOTAG);
    ptr = doPut4(ptr, (uint32_t)f);
    r = sendRecv(txbuf, ptr);
    if (r < 0 || txbuf[0] != Rclunk) {
        return -1;
    }
    return 0;
}

int fs_read(fs_file *f, uint8_t *buf, int count)
{
    uint8_t *ptr;
    int totalread = 0;
    int curcount;
    int r;
    int left;
    uint32_t oldlo;
    while (count > 0) {
        ptr = doPut4(txbuf, 0); // space for size
        ptr = doPut1(ptr, Tread);
        ptr = doPut2(ptr, NOTAG);
        ptr = doPut4(ptr, (uint32_t)f);
        ptr = doPut4(ptr, f->offlo);
        ptr = doPut4(ptr, f->offhi);
        left = maxlen - (ptr + 4 - txbuf);
        if (count < left) {
            curcount = count;
        } else {
            curcount = left;
        }
        ptr = doPut4(ptr, curcount);
        r = sendRecv(txbuf, ptr);
        if (r < 0) return r;
        ptr = txbuf;
        if (*ptr++ != Rread) {
            ser.printf("No read response\n");
            return -1;
        }
        ptr += 2; // skip tag
        r = FETCH4(ptr); ptr += 4;
        if (r < 0 || r > curcount) {
            ser.printf("read: r=%d\n", r);
            return -1;
        }
        if (r == 0) {
            // EOF reached
            break;
        }
        memcpy(buf, ptr, r);
        buf += r;
        totalread += r;
        count -= r;
        oldlo = f->offlo;
        f->offlo = oldlo + r;
        if (f->offlo < oldlo) {
            f->offhi++;
        }
    }
    return totalread;
}

// test program
int main()
{
    int r;
    char buf[80];
    _clkset(0x010007f8, 160000000);
    ser.start(63, 62, 0, 230400);
    ser.printf("9p test program...\n");
    ser.printf("Initializing...\n");
    r = fs_init();
//    ser.printf("Init returned %d\n", r);
//    pausems(1000);
    if (r == 0) {
        r = fs_open(&testfile, (char *)"fs9p.h", 0);
        pausems(10);
        ser.printf("fs_open returned %d\n", r);
    }
    if (r == 0) {
        int i;
        // read the file and show it
        ser.printf("FILE CONTENTS:\r\n");
        pausems(100);
        do {
            r = fs_read(&testfile, buf, sizeof(buf));
            for (i = 0; i < r; i++) {
                ser.tx(buf[i]);
            }
        } while (r > 0);
        pausems(10);
        ser.printf("EOF\r\n");
        fs_close(&testfile);
    }
    return 0;
}
