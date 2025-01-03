//---------------------------------------------------------------------------
//
//  Module: nabu.h
//
//  Purpose:
//     This is the header file for NABU Adaptor capabilities.
//
//  Development Team:
//     Chris Lenderman
//
//  History:   Date       Author      Comment
//             12/23/24   ChrisL      Wrote it.
//
//---------------------------------------------------------------------------

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <io.h>
#include <conio.h>
#include "serial.h"

int resetNabuState() ;
void processNABU( unsigned char, char* ) ;

void freeLoadedPackets(void);
void createTimeSegment(void);
void sendPacket(void);

int WriteCommBlock(unsigned char* bByte, int nByteLen);
int WriteCommByte(unsigned char bByte);

#define PACKET_HEADER_SIZE 16
#define PACKET_DATA_SIZE 993
#define PACKET_CRC_SIZE 2
#define TIME_SEGMENT_SIZE 29

