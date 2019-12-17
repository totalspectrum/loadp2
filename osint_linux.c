/**
 * @file osint_linux.c
 *
 * Serial I/O functions used by PLoadLib.c
 *
 * Copyright (c) 2009 by John Steven Denson
 * Modified in 2011 by David Michael Betz
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
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/timeb.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/time.h>
#include <dirent.h>
#include <limits.h>
#include <signal.h>
#include <time.h>

#ifdef MACOSX
#include <IOKit/serial/ioss.h>
#endif

#include "osint.h"

typedef int HANDLE;
static HANDLE hSerial = -1;
static struct termios old_sparm;
static unsigned long last_baud = -1;
static char last_port[PATH_MAX];

/* normally we use DTR for reset but setting this variable to non-zero will use RTS instead */
static int use_rts_for_reset = 0;

void serial_use_rts_for_reset(int use_rts)
{
    use_rts_for_reset = use_rts;
}

static void chk(char *fun, int sts)
{
    if (sts != 0)
        printf("%s failed\n", fun);
}

//
// on Linux, changing baud messes with the DTR/RTS lines, which
// resets the P2 board :(. This function used to exist to force the
// loader baud to match the user baud. We work around the problem
// now by re-opening the handle, but this legacy code is left over.
//
int get_loader_baud(int ubaud, int lbaud)
{
    return lbaud;
}

static void sigint_handler(int signum)
{
    serial_done();
    exit(1);
}

#if !defined(MACOSX)
// MACOSX uses a different method to set baud
static int set_baud(struct termios *sparm, unsigned long baud)
{
    int tbaud = 0;

    switch(baud) {
        case 0: // default
            tbaud = B115200;
            break;
#ifdef B2000000
        case 2000000:
            tbaud = B2000000;
            break;
#endif
#ifdef B921600
        case 921600:
            tbaud = B921600;
            break;
#endif
#ifdef B576000
        case 576000:
            tbaud = B576000;
            break;
#endif
#ifdef B500000
        case 500000:
            tbaud = B500000;
            break;
#endif
#ifdef B460800
        case 460800:
            tbaud = B460800;
            break;
#endif
#ifdef B230400
        case 230400:
            tbaud = B230400;
            break;
#endif
        case 115200:
            tbaud = B115200;
            break;
        case 57600:
            tbaud = B57600;
            break;
        case 38400:
            tbaud = B38400;
            break;
        case 19200:
            tbaud = B19200;
            break;
        case 9600:
            tbaud = B9600;
            break;
        default:
            tbaud = baud;
            printf("Unsupported baudrate %lu. Use ", baud);
#ifdef B921600
            printf("921600, ");
#endif
#ifdef B576000
            printf("576000, ");
#endif
#ifdef B500000
            printf("500000, ");
#endif
#ifdef B460800
            printf("460800, ");
#endif
#ifdef B230400
            printf("230400, ");
#endif
            printf("115200, 57600, 38400, 19200, or 9600\n");
            serial_done();
            exit(2);
            break;
    }

    /* set raw input */
    chk("cfsetispeed", cfsetispeed(sparm, tbaud));
    chk("cfsetospeed", cfsetospeed(sparm, tbaud));
    return 1;
}
#endif /* !MACOSX */

/**
 * open serial port
 * @param port - COMn port name
 * @param baud - baud rate
 * @returns 1 for success and 0 for failure
 */
int serial_init(const char* port, unsigned long baud)
{
    struct termios sparm;

    /* open the port */
#if defined(MACOSX)
    speed_t speed = (speed_t) baud;

    hSerial = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
#else
    hSerial = open(port, O_RDWR | O_NOCTTY | O_NDELAY | O_NONBLOCK);
#endif
    if(hSerial == -1) {
        //printf("error: opening '%s' -- %s\n", port, strerror(errno));
        return 0;
    }
    last_baud = baud;
    strncpy(last_port, port, PATH_MAX-1);
    
    signal(SIGINT, sigint_handler);    
    fcntl(hSerial, F_SETFL, 0);
    
    /* get the current options */
    chk("tcgetattr", tcgetattr(hSerial, &old_sparm));
    sparm = old_sparm;
    
    /* set raw input */
    cfmakeraw(&sparm);
    sparm.c_cc[VTIME] = 0;
    sparm.c_cc[VMIN] = 1;

#if !defined(MACOSX)    
    if (!set_baud(&sparm, baud)) {
        close(hSerial);
        printf("failure setting baud %ld\n", (long)baud);
        return 0;
    }
#endif
    
    /* set the options */
    chk("tcsetattr", tcsetattr(hSerial, TCSANOW, &sparm));

#ifdef MACOSX
    if (ioctl(hSerial, IOSSIOSPEED, &speed) != 0)
    {
        close(hSerial);
        printf("failure setting speed %ld\n", (long)baud);
        return 0;
    }
#endif    
    chk("tcflush", tcflush(hSerial, TCIFLUSH));
    
    return 1;
}

/**
 * change the baud rate of the serial port
 * @param baud - baud rate
 * @returns 1 for success and 0 for failure
 * On Linux this gets tricky, changiing baud on an already open
 * handle drops DTR and resets the board. So we have to initialize
 * new connection and then close the old one.
 */
int serial_baud(unsigned long baud)
{
    if (baud != last_baud) {
        HANDLE oldSerial = hSerial;
        if (!serial_init(last_port, baud)) {
            printf("serial_init of %s failed\n", last_port);
            exit(1);
        }
        close(oldSerial);
    }
    return 1;
}

/**
 * flush all input
 */
int flush_input(void)
{
    return tcflush(hSerial, TCIFLUSH);
}

/**
 * wait for tx buffer to be empty
 */
int wait_drain(void)
{
    return tcdrain(hSerial);
}

/**
 * close serial port
 */
void serial_done(void)
{
    if (hSerial != -1) {
        tcflush(hSerial, TCIOFLUSH);
        //tcsetattr(hSerial, TCSANOW, &old_sparm);
        ioctl(hSerial, TIOCNXCL);
        close(hSerial);
        hSerial = -1;
    }
}

/**
 * receive a buffer
 * @param buff - char pointer to buffer
 * @param n - number of bytes in buffer to read
 * @returns number of bytes read
 */
int rx(uint8_t* buff, int n)
{
    ssize_t bytes = read(hSerial, buff, n);
    if(bytes < 1) {
        printf("Error reading port: %d\n", (int)bytes);
        return 0;
    }
    return (int)bytes;
}

/**
 * transmit a buffer
 * @param buff - char pointer to buffer
 * @param n - number of bytes in buffer to send
 * @returns zero on failure
 */
int tx(uint8_t* buff, int n)
{
    ssize_t bytes;
#if 0
    int j = 0;
    while(j < n) {
        printf("%02x ",buff[j++]);
    }
    printf("tx %d byte(s)\n",n);
#endif
    bytes = write(hSerial, buff, n);
    if(bytes != n) {
        printf("Error writing port\n");
        return 0;
    }
    return (int)bytes;
}

/**
 * receive a buffer with a timeout
 * @param buff - char pointer to buffer
 * @param n - number of bytes in buffer to read
 * @param timeout - timeout in milliseconds
 * @returns number of bytes read or SERIAL_TIMEOUT
 */
int rx_timeout(uint8_t* buff, int n, int timeout)
{
    ssize_t bytes = 0;
    struct timeval toval;
    fd_set set;

    FD_ZERO(&set);
    FD_SET(hSerial, &set);

    toval.tv_sec = timeout / 1000;
    toval.tv_usec = (timeout % 1000) * 1000;

    if (select(hSerial + 1, &set, NULL, NULL, &toval) > 0) {
        if (FD_ISSET(hSerial, &set))
            bytes = read(hSerial, buff, n);
    }

    return (int)(bytes > 0 ? bytes : SERIAL_TIMEOUT);
}

/**
 * hwreset ... resets Propeller hardware using DTR or RTS
 * @param sparm - pointer to DCB serial control struct
 * @returns void
 */
void hwreset(void)
{
    int cmd = use_rts_for_reset ? TIOCM_RTS : TIOCM_DTR;
    ioctl(hSerial, TIOCMBIS, &cmd); /* assert bit */
    msleep(2);
    ioctl(hSerial, TIOCMBIC, &cmd); /* clear bit */
    msleep(2);
    ioctl(hSerial, TIOCMBIS, &cmd); /* assert bit */
    msleep(2);
    tcflush(hSerial, TCIFLUSH);
}

/**
 * sleep for ms milliseconds
 * @param ms - time to wait in milliseconds
 */
void msleep(int ms)
{
#if 0
    volatile struct timeb t0, t1;
    do {
        ftime((struct timeb*)&t0);
        do {
            ftime((struct timeb*)&t1);
        } while (t1.millitm == t0.millitm);
    } while(ms-- > 0);
#else
    usleep(ms * 1000);
#endif
}

/**
 * simple terminal emulator
 */
void terminal_mode(int check_for_exit, int pst_mode)
{
    struct termios oldt, newt;
    char buf[128], realbuf[256]; // double in case buf is filled with \r in PST mode
    ssize_t cnt;
    fd_set set;
    int exit_char = 0xdead; /* not a valid character */
    int sawexit_char = 0;
    int sawexit_valid = 0; 
    int exitcode = 0;
    int continue_terminal = 1;

    if (isatty(STDIN_FILENO)) {
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        cfmakeraw(&newt);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    }
    if (check_for_exit)
      {
        exit_char = 0xff;
      }

#if 0
    /* make it possible to detect breaks */
    tcgetattr(hSerial, &newt);
    newt.c_iflag &= ~IGNBRK;
    newt.c_iflag |= PARMRK;
    tcsetattr(hSerial, TCSANOW, &newt);
#endif

    do {
        FD_ZERO(&set);
        FD_SET(hSerial, &set);
        FD_SET(STDIN_FILENO, &set);
        if (select(hSerial + 1, &set, NULL, NULL, NULL) > 0) {
            if (FD_ISSET(hSerial, &set)) {
                if ((cnt = read(hSerial, buf, sizeof(buf))) > 0) {
                    int i;
                    // check for breaks
                    ssize_t realbytes = 0;
                    for (i = 0; i < cnt; i++) {
                      if (sawexit_valid)
                        {
                          exitcode = buf[i];
                          //printf("exitcode: %02x\n", buf[i]);
                          continue_terminal = 0;
                        }
                      else if (sawexit_char) {
                        //printf("exitchar 2: %02x\n", buf[i]);
                        if (buf[i] == 0) {
                          sawexit_valid = 1;
                        } else if (buf[i] == 1) {
                            int r = u9fs_process(cnt - (i+1), &buf[i+1]);
                            i += (r-1);
                            sawexit_char = 0;
                            break;
                        } else {
                          realbuf[realbytes++] = exit_char;
                          realbuf[realbytes++] = buf[i];
                          sawexit_char = 0;
                        }
                      } else if (((int)buf[i] & 0xff) == exit_char) {
                        //printf("exitchar: %02x\n", buf[i]);
                        sawexit_char = 1;
                      } else {
                        realbuf[realbytes++] = buf[i];
                        if (pst_mode && buf[i] == '\r')
                            realbuf[realbytes++] = '\n';
                      }
                    }
                    if (realbytes > 0) {
                        write(fileno(stdout), realbuf, realbytes);
                    }
                }
            }
            if (FD_ISSET(STDIN_FILENO, &set)) {
                if ((cnt = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
                    int i;
                    for (i = 0; i < cnt; ++i) {
                        //printf("%02x\n", buf[i]);
                        if (buf[i] == EXIT_CHAR0)
                            goto done;
                    }
                    write(hSerial, buf, cnt);
                }
            }
        }
    } while (continue_terminal);

done:
    if (isatty(STDIN_FILENO)) {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    }

    if (sawexit_valid)
      {
        exit(exitcode);
      }
    
}

unsigned long long
elapsedms(void)
{
    struct timeval t;

    if (!gettimeofday(&t, NULL)) {
        // how could this fail??
        return time(NULL) * 1000ULL;
    }
    return 1000 * (unsigned long long)t.tv_sec + ((unsigned long long)t.tv_usec/1000);
}
