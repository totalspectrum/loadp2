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

// maximum length we're willing to send/receive from host
// write: 4 + 1 + 2 + 4 + 8 + 4 + 1024 = 1048

#define MAXLEN 1048

#endif
