//
// simple test program for 9p access to host files
//

#include <string.h>
#include <stdint.h>
#include "fs9p.h"

struct __using("spin/SmartSerial") ser;

typedef struct fsfile {
    unsigned offset;
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

// send a special signal to the host to switch to 9P mode
void doSendBuffer(uint8_t *startbuf, uint8_t *endbuf) {
    uint32_t len = endbuf - startbuf;
    doPut4(startbuf, len);
    ser.tx(0xff);
    ser.tx(0x01);
    while (len>0) {
        ser.tx(*startbuf++);
        --len;
    }
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

int getResponse(uint8_t *buf, int maxlen)
{
    int len = doGet4() - 4;
    int left = len;
    int i = 0;
    while (left > 0 && i < maxlen) {
        buf[i++] = doGet1();
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
    doSendBuffer(txbuf, ptr);

    ptr = txbuf;
    len = getResponse(ptr, MAXLEN);

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
    doSendBuffer(txbuf, ptr);
    
    ptr = txbuf;
    len = getResponse(txbuf, MAXLEN);
    if (ptr[0] != Rattach) {
        ser.printf("Unable to attach\n");
        return -1;
    }
    return 0;
}

// test program
int main()
{
    int r;
    _clkset(0x010007f8, 160000000);
    ser.start(63, 62, 0, 230400);
    ser.printf("9p test program...\n");
    ser.printf("Initializing...\n");
    r = fs_init();
    ser.printf("Init returned %d\n", r);
    return 0;
}
