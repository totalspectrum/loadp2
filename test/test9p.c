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

// send 1 byte to the host
void doSend1(unsigned x) {
    ser.tx(x);
}

// send a special signal to the host to switch to 9P mode
void doSendHeader(void) {
    doSend1(0xff);
    doSend1(0x01);
}

// send a 16 bit integer to the host
void doSend2(unsigned x) {
    doSend1(x & 0xff);
    doSend1((x>>8) & 0xff);
}

// send a 32 bit integer to the host
void doSend4(unsigned x) {
    doSend1(x & 0xff);
    doSend1((x>>8) & 0xff);
    doSend1((x>>16) & 0xff);
    doSend1((x>>24) & 0xff);
}

void doSendStr(const char *s) {
    unsigned L = strlen(s);
    unsigned i = 0;
    doSend2(L);
    for (i = 0; i < L; i++) {
        doSend1(*s++);
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

// receive an unsigned short
unsigned doGet2()
{
    unsigned r;
    r = doGet1();
    r = r | (doGet1() << 8);
    return r;
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

int getResponseHeader(uint8_t *buf, int maxlen)
{
    int len = doGet4();
    int left = len - 4;
    int i = 0;
    while (left > 0 && i < maxlen) {
        buf[i++] = doGet1();
        --left;
    }
    return left;
}

void skipLeft(int left)
{
    while (left > 0) {
        doGet1();
        --left;
    }
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
    int size = 4 + 1 + 2 + 4 + 2 + 6;
    static uint8_t buf[20];
    int remain;
    unsigned msize;
    unsigned s;
    unsigned tag;
    
    doSendHeader();
    doSend4(size);
    doSend1(Tversion);
    doSend2(NOTAG);
    doSend4(MAXLEN);
    doSendStr("9P2000");

    remain = getResponseHeader(buf, sizeof(buf));
    skipLeft(remain);
    
    if (buf[0] != Rversion) {
        ser.printf("No version response from host\n");
        ser.printf("remain=%x buf[] = %x %x %x %x ",
                   remain, buf[0], buf[1], buf[2], buf[3]);
        ser.printf(" %x %x %x %x\n",
                   buf[4], buf[5], buf[6], buf[7]);
        return -1;
    }
    tag = FETCH2(&buf[1]);
    msize = FETCH4(&buf[3]);

    s = FETCH2(&buf[7]);
    if (s != 6 || 0 != strncmp(&buf[9], "9P2000")) {
        ser.printf("Bad version response from host: s=%d ver=%s\n", s, &buf[9]);
        return -1;
    }
    if (msize < 64 || msize > MAXLEN) {
        ser.printf("max message size %u is out of range\n", msize);
        return -1;
    }
    maxlen = msize;

    // OK, try to attach
    size = 4 + 1 + 2 + 4 + 4 + 2 + strlen("user") + 2 + 0;
    doSendHeader();
    doSend4(size);
    doSend1(Tattach);
    doSend2(NOTAG);  // not sure about this one...
    doSend4((uint32_t)&rootdir); // our FID
    doSend4(NOFID); // no authorization requested
    doSendStr("user");
    doSendStr(""); // no aname

    remain = getResponseHeader(buf, sizeof(buf));
    if (remain > 0) {
        skipLeft(remain);
    }
    if (buf[0] != Rattach) {
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
