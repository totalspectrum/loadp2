This directory contains various board support programs, e.g.
for programming SPI flash or SD cards.

All of them expect the program length and then data to be in memory
starting at $8000, which means they must fit in the first 32K.

P2ES_flashloader.spin2:
  P2 ES Eval board SPI flash programmer. May not work with other SPI
  flash parts

sdcard/:
  Code for writing to SD card file _BOOT_P2.BIX. Should be generic
  enough to work on any board with the same SD pins as the P2 ES (you
  may need to tweak the clock setting though).
