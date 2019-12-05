# loadp2: a loader for the Parallax Propeller 2

Written by Dave Hein
Modified by Eric R. Smith

## Introduction

loadp2 is used to load programs to the memory of a Parallax Propeller 2 ("P2") chip over a serial connection. It may also optionally execute programs from the P2 ROM (such as the built-in Forth interpreter TAQOZ) and/or send a scripted set of keystrokes to the application after loading.

## Usage

```
usage: loadp2
         [ -p port ]               serial port
         [ -b baud ]               user baud rate (default is 115200)
         [ -l baud ]               loader baud rate (default is 2000000)
         [ -f clkfreq ]            clock frequency (default is 80000000)
         [ -m clkmode ]            clock mode in hex (default is ffffffff)
         [ -s address ]            starting address in hex (default is 0)
         [ -t ]                    enter terminal mode after running the program
         [ -v ]                    enable verbose mode
         [ -k ]                    wait for user input before exit
         [ -q ]                    quiet mode: also checks for exit sequence
         [ -n ]                    no reset; skip any hardware reset
         [ -? ]                    display a usage message and exit
         [ -xDEBUG ]               enter ROM debug monitor
         [ -xTAQOZ ]               enter ROM version of TAQOZ
         [ -CHIP ]                 set load mode for CHIP
         [ -FPGA ]                 set load mode for FPGA
         [ -PATCH ]                patch in clock frequency and serial parms
         [ -NOZERO ]               do not clear memory before download
         [ -SINGLE ]               set load mode for single stage
         filespec                  file(s) to load
	 [ -e script ]             send script characters after loading
```

## Loading multiple files

In `-CHIP` mode (the default), filespec may optionally be multiple files with address specifiers, such as:
```
@ADDR=file1,@ADDR=file2,@ADDR+file3
```
Here ADDR is a hex address at which to load the next file, followed by = or +
If it is followed by + then the size of the file is put in memory followed by
the file data. This feature is useful for loading data that a program wishes
to act on. For example, a VGA program which displays data from $1000 may be
loaded with a filespec of:
```
    @0=vgacode.bin,@1000=picture.bmp
```
The main executable code must always be specified first

## Scripts

A script of commands to perform after the download may be specified With the `-e` option. The various commands allowed
are specified below. Each command takes one argument, which is an escaped string bracketed either by `(` and `)` or by `{` and `}`. For example, to pause for 10 milliseconds one would use the command `pausems(10)` or `pausems{10}`. To send a right parenthesis one would use either `send{)}` or `send(^))`; note that in the second form we have to escape the parenthesis with `^`, otherwise it would be interpreted as the end of the string.

### Commands

The specific commands are discussed later, but here is a list of them:
```
binfile(fname):    send a binary file to the P2
pauseafter(N):     insert a 1ms pause after N characters transmitted
pausems(N):        delay for N milliseconds
recv(string):      wait until string is received
recvtimeout(N):    set a timeout in ms for the recv() command
scriptfile(fname): read script commands from file "fname"
send(string):      send a string to the P2
textfile(fname):   send contents of a text file to the P2
```

### Strings

Within scripts several special sequences are interpreted:
```
^^: send a caret (^) symbol
^): send a right parenthesis
^}: send a right bracket
^(: send a left parenthesis
^A: send control-A; similarly for ^B, ^C, etc.
^a: send control-A; similarly for ^b, ^c, etc.
^0, ^1, etc.: starts a decimal escape sequence. The decimal number is translated to a single ASCII character and sent
```
Note that it is difficult to use decimal escape sequences that are followed by digits; for example to send ASCII 11 followed by the digit 2 one cannot do `send(^112)` because this will be interpreted as sending an ASCII 112. You may work around this by translating the digit into a further escape sequence, e.g. `send(^11^50)` (the ASCII code for "2" is 50).

### binfile

Sends the contents of a file as binary (no translation performed on the contents). So `binfile(foo.txt)` sends the contents of the file `foo.txt` exactly as they are. If lines end in DOS style carriage return + line feed, both of those characters (ASCII 13 and ASCII 10) will be sent.

Note that the `pauseafter(N)` command may be used to specify that a 1 millisecond pause should be inserted after every N characters sent. The default is not to insert pauses.

Example:
```
binfile(myfile.bin)
```

### pauseafter

Specifies a count of characters to pause after during any file transmission. That is, if you call `pauseafter(10)` then after every 10 characters sent a 1 millisecond pause is inserted. This is useful for throttling scripts that are sending to programs that cannot process data very quickly.

The default is 0, which indicates that no pauses should be inserted.

### pausems

Wait for a number of milliseconds, e.g. `pausems(100)` will wait for 100 milliseconds.

### recv

Wait for the other end to send a string. For example `recv(>>>)` waits for the other end to send the string `>>>`. If the requested string is not received within the time specified by the last `recvtimeout` call, then fail. The default timeout value is 1000 (i.e. one second).

### recvtimeout

Set the time (in milliseconds) for subsequent `recv` calls to time out. A value of 0 causes `recv` to never time out.

### scriptfile

Read and execute the contents of a file as a script. If the script fails an error message will be printed, but the main script will continue executing. However, if the script file itself cannot be opened or read then the calling script will terminate.

Script files may be at most 256K bytes long.

### send

Sends a string as if the user typed it. Note that the usual string escape sequences are interpreted. So to send `hello` and then a carriage return, use `send(hello^M)` or `send(hello^13)`.

Note that the `pauseafter(N)` command may be used to specify that a 1 millisecond pause should be inserted after every N characters. The default is not to insert pauses.

### textfile

Sends the contents of a file. The name of the file is escaped with the usual `^` sequences. End of line markers in the file are translated to control-M.

Note that the `pauseafter(N)` command may be used to specify that a 1 millisecond pause should be inserted after every N characters sent. The default is not to insert pauses.

### Script Examples

Start TAQOZ, pause for 1000 milliseconds, send the file "myfile.fth", and then enter terminal mode:
```
loadp2 -b230400 -xTAQOZ -e "pausems(1000) textfile(myfile.fth)" -t
```

Start upython, pause for 1000 milliseconds, wait for the prompt >>>, then send the string `print('hi')` and a carriage return:
```
loadp2 -b230400 upython.binary -e "pausems(1000) recv(>>> ) send{print('hi')^M}" -t
```

## Compiling loadp2

Use the standard Makefile.

To cross compile do
```
  make CROSS=win32
  make CROSS=macosx
  make CROSS=linux32
```

To build natively on Mac OS X, do something like:
```
   make CC="gcc -DMACOSX"
```
