#include <dos.h>

int isTandy1000();
int cpuIs8088or8086();

enum VideoCard {
    videoCard_mda = 0,
    videoCard_cga = 1,
    videoCard_pcJr = 2,
    videoCard_tandy1000 = 3,
    videoCard_tandySLTL = 4,
    videoCard_ega = 5,
    videoCard_vga = 6
};
