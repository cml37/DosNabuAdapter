#ifndef _NABUTILS_H
#define _NABUTILS_H

#include <dos.h>

int isTandy1000() ;
int cpuIs8088or8086() ;
int detectVideoMode() ;

enum VideoCard {
   videoCard_mda = 0,
   videoCard_cga = 1,
   videoCard_pcJr = 2,
   videoCard_tandy1000 = 3,
   videoCard_tandySLTL = 4,
   videoCard_ega = 5,
   videoCard_vga = 6
};

#endif
