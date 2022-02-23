/**
 * @file osint.h
 *
 * Serial I/O functions used by PLoadLib.c
  *
 * Copyright (c) 2009 by John Steven Denson
 * Modified in 2011 by David Michael Betz
 * Modified in 2022 by Eric R. Smith
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
#ifndef SERIAL_IO_H__
#define SERIAL_IO_H__

#include <stdint.h>

/* serial i/o definitions */
#define SERIAL_TIMEOUT  -1
#define EXIT_CHAR0 29 /* CTRL-] exits from terminal */
#define EXIT_CHAR1 26 /* CTRL-Z also exits from terminal */

/* serial i/o routines */
void serial_use_rts_for_reset(int use_rts);
int serial_find(const char* prefix, int (*check)(const char* port, void* data), void* data);
int serial_init(const char *port, unsigned long baud);
int serial_baud(unsigned long baud);
void serial_done(void);
int tx(uint8_t* buff, int n);
int rx(uint8_t* buff, int n);
int rx_timeout(uint8_t* buff, int n, int timeout);
void hwreset(void);
int flush_input(void);
int wait_drain(void);

/* terminal mode */
void terminal_mode(int check_for_exit, int pst_mode);

/* miscellaneous functions */
void msleep(int ms);

/* fetch elapsed milliseconds since some point in the past */
unsigned long long elapsedms(void);

/* external filesystem functions in the u9fs/u9fs.c */
int u9fs_init(char *user_root);
int u9fs_process(int count, char *buf);

/* in loadp2.c */
extern int waitAtExit; // if nonzero prompt before exiting
void promptexit(int status);

#endif
