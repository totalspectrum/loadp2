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

## Key Scripts

A script of keys to send after the download may be specified With the `-e` option. This is a string which will be sent to the P2 after the executable is started (and after a short delay), as if the user typed it.

Within the string several special sequences are interpreted:
```
^A: send control-A; similarly for ^B, ^C, etc.
^a: send control-A
^^: send a caret (^) symbol
^#: wait for 100 milliseconds
^{filename}: send the contents of the file "filename"
^/string/ or ^,string,: wait up to 1 second for the given string to appear
```

Note that filenames are sent as text, with CR+LF converted to CR.

### Examples

Start TAQOZ, send the file "myfile.fth", and then enter terminal mode:
```
loadp2 -b230400 -xTAQOZ -e^{myfile.fth} -t
```
