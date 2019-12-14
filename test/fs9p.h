#ifndef FS9P_H
#define FS9P_H

#define NOTAG 0xffffU
#define NOFID 0xffffffffU

enum {
    Tversion = 100,
    Rversion,
    Tauth = 102,
    Rauth,
    Tattach = 104,
    Rattach,
    Terror = 106,
    Rerror,
    Tflush = 108,
    Rflush,
    Twalk = 110,
    Rwalk,
    Topen = 112,
    Ropen,
    Tcreate = 114,
    Rcreate,
    Tread = 116,
    Rread,
    Twrite = 118,
    Rwrite,
    Tclunk = 120,
    Rclunk,
};

// maximum length we're willing to receive from host
// we actually want to allow long read/write messages for
// efficiency, so let's allow up to 64K
#define MAXLEN 65536

#endif
