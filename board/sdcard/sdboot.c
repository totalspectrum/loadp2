/*----------------------------------------------------------------------*/
/* Foolproof FatFs sample project for AVR              (C)ChaN, 2014    */
/* Modified by Eric Smith for P2 (C) Total Spectrum Software 2020       */
/*----------------------------------------------------------------------*/

#include <stdio.h>
#include <propeller2.h>	/* Device specific declarations */
#include "ff.h"		/* Declarations of FatFs API */

FATFS FatFs;		/* FatFs work area needed for each volume */
FIL Fil;			/* File object needed for each open file */

#define FILENAME "_BOOT_P2.BIX"
#define BASEADDR 0x8000

void exit(int n)
{
    for(;;) ;
}

extern int sdmmc_is_present(void);

#define MSG_DELAY 20000000
int main (void)
{
	UINT bw;
	FRESULT fr;
        uint8_t *data;
        uint32_t siz;
        int c;
        
        _clkset(0x010007f8, 160000000);
        
        printf("SD Updater\n");
        while (!sdmmc_is_present()) {
            printf("Insert card and press enter to continue...\n");
            do {
                c = getchar();
            } while (c != 10 && c != 13);
        }

        // wait a little bit
        _waitx(MSG_DELAY);
        
        fr = f_mount(&FatFs, "", 0);		/* Give a work area to the default drive */
        if (fr != FR_OK) {
            printf("Unable to mount card\n");
            exit(1);
        }
        printf("Card mounted\n");
        // wait a little bit
        _waitx(MSG_DELAY);

        do {
            fr = f_unlink(FILENAME);
            if (fr == FR_NOT_READY) {
                printf("Drive not ready, retrying\n");
                _waitx(MSG_DELAY);
            }
        } while (fr == FR_NOT_READY);

        siz = *(uint32_t *)BASEADDR;
        data = (uint8_t *)(BASEADDR+4);

        if (siz == 0) {
            if (fr == FR_NO_FILE) {
                printf("%s not found\n", FILENAME);
            } else if (fr != FR_OK) {
                printf("Error %d while deleting %s\n", fr, FILENAME);
                exit(1);
            } else {
                printf("Deleted %s\n", FILENAME);
            }
            exit(0);
        }
	fr = f_open(&Fil, FILENAME, FA_WRITE | FA_CREATE_ALWAYS);	/* Create a file */
	if (fr == FR_OK) {
            printf("writing %s...", FILENAME);
            fr = f_write(&Fil, data, siz, &bw);	/* Write data to the file */
            if (fr != FR_OK) {
                printf("WRITE ERROR: f_write returned %d\n", fr);
            }
            fr = f_close(&Fil);							/* Close the file */
            if (fr != FR_OK) {
                printf("WRITE ERROR: f_close returned %d\n", fr);
            } else if (bw != siz) {
                printf("WRITE ERROR: tried to write %d, actually wrote %d bytes\n", siz, bw);
            } else {
                printf("OK\n");
            }
	} else {
            printf("error %d opening %s\n", fr, FILENAME);
        }
        _waitx(_clockfreq());
        _reboot();
}
