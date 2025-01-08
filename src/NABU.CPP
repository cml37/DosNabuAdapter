//---------------------------------------------------------------------------
//
//  Module: nabu.cpp
//
//  Purpose:
//     Capabilities to support driving a NABU adaptor
//
//  Development Team:
//     Chris Lenderman
//
//  History:   Date       Author      Comment
//             12/23/24   ChrisL      Wrote it.
//
//---------------------------------------------------------------------------

#include "nabu.h"
#include "utils.h"
#include "nabuhtgt.h"

extern "C"
{
   #include "nabutils.h"
}

// TODOs
// Anywhere we use a fixed size array, scrutinize it to see if we can
// calculate dynamically instead
// Clean up ugly code

// The cycle path
char cyclePath[ 20 ] = "C:\\cycle\\" ;

// The optional host and path
char hostAndPath[ 200 ] = "nabu.retrotechchris.com/cycle2" ;

// The COM port
int com = 0 ;

// Base on the way that byte reads are not blocking,
// I've come up with a scheme to track the current processing
// command byte as well as determine which stage we are in for the processing byte
unsigned char lastResetProcessingByte = 0x0 ;
unsigned char processingByte;
int processingByteInitialized = 0 ;
int processingStage = 0 ;

// The current packet and segment number for a file request
int           packetNumber ;
unsigned long segmentNumber ;

// The current loaded packet for a file request, along with its length
unsigned char *loadedPacketPtr = NULL ;
int            loadedPacketLength = 0 ;

long cycleCrcTable[] =
    { 0L, 4129L, 8258L, 12387L, 16516L, 20645L, 24774L, 28903L, 33032L, 37161L, 41290L,
      45419L, 49548L, 53677L, 57806L, 61935L, 4657L, 528L, 12915L, 8786L, 21173L,
      17044L, 29431L, 25302L, 37689L, 33560L, 45947L, 41818L, 54205L, 50076L,
      62463L, 58334L, 9314L, 13379L, 1056L, 5121L, 25830L, 29895L, 17572L, 21637L,
      42346L, 46411L, 34088L, 38153L, 58862L, 62927L, 50604L, 54669L, 13907L, 9842L,
      5649L, 1584L, 30423L, 26358L, 22165L, 18100L, 46939L, 42874L, 38681L, 34616L,
      63455L, 59390L, 55197L, 51132L, 18628L, 22757L, 26758L, 30887L, 2112L, 6241L,
      10242L, 14371L, 51660L, 55789L, 59790L, 63919L, 35144L, 39273L, 43274L,
      47403L, 23285L, 19156L, 31415L, 27286L, 6769L, 2640L, 14899L, 10770L, 56317L,
      52188L, 64447L, 60318L, 39801L, 35672L, 47931L, 43802L, 27814L, 31879L,
      19684L, 23749L, 11298L, 15363L, 3168L, 7233L, 60846L, 64911L, 52716L, 56781L,
      44330L, 48395L, 36200L, 40265L, 32407L, 28342L, 24277L, 20212L, 15891L,
      11826L, 7761L, 3696L, 65439L, 61374L, 57309L, 53244L, 48923L, 44858L, 40793L,
      36728L, 37256L, 33193L, 45514L, 41451L, 53516L, 49453L, 61774L, 57711L, 4224L,
      161L, 12482L, 8419L, 20484L, 16421L, 28742L, 24679L, 33721L, 37784L, 41979L,
      46042L, 49981L, 54044L, 58239L, 62302L, 689L, 4752L, 8947L, 13010L, 16949L,
      21012L, 25207L, 29270L, 46570L, 42443L, 38312L, 34185L, 62830L, 58703L,
      54572L, 50445L, 13538L, 9411L, 5280L, 1153L, 29798L, 25671L, 21540L, 17413L,
      42971L, 47098L, 34713L, 38840L, 59231L, 63358L, 50973L, 55100L, 9939L, 14066L,
      1681L, 5808L, 26199L, 30326L, 17941L, 22068L, 55628L, 51565L, 63758L, 59695L,
      39368L, 35305L, 47498L, 43435L, 22596L, 18533L, 30726L, 26663L, 6336L, 2273L,
      14466L, 10403L, 52093L, 56156L, 60223L, 64286L, 35833L, 39896L, 43963L,
      48026L, 19061L, 23124L, 27191L, 31254L, 2801L, 6864L, 10931L, 14994L, 64814L,
      60687L, 56684L, 52557L, 48554L, 44427L, 40424L, 36297L, 31782L, 27655L,
      23652L, 19525L, 15522L, 11395L, 7392L, 3265L, 61215L, 65342L, 53085L, 57212L,
      44955L, 49082L, 36825L, 40952L, 28183L, 32310L, 20053L, 24180L, 11923L,
      16050L, 3793L, 7920 };

char* errors[] =
{
   "Successful",
   "Unknown error",
   "Port not open",
   "Port already open",
   "No UART found on that comport",
   "Invalid comport",
   "Invalid BPS",
   "Invalid data bits",
   "Invalid parity",
   "Invalid stop bits",
   "Invalid handshaking",
   "Invalid fifo threshold",
   "Passed in a NULL pointer",
   "",
   "",
   ""
};

// Creates a directory for storing the cycles.
bool makeCycleDirectory( char* directory )
{
   struct stat st ;
   char strip [ 100 ] ;

   // Strip off the trailing slash before making the directory
   strncpy( strip, directory, strlen( directory ) -1 ) ;
   strip[ strlen(directory) - 1 ] = 0 ;

   if ( stat( strip, &st ) == -1 )
   {
      if ( mkdir( strip ) == -1 )
      {
         printf( "Could not make cycle directory: \n%s %d", strip, strlen( directory) ) ;
         return false;
      }
   }
   return true;
}

// The entry point to the program
int main( int argc, char *argv[] )
{
   int rc ;
   char ch ;

   if( argc < 2 )
   {
      printf( "Usage: dosnabu <com number> <optional cycle path, defaults to C:\\cycle\\> <optional download host and path> \n" ) ;
      return 0 ;
   }

   if ( isTandy1000() )
   {
      printf("Tandy 1000 detected\n") ;
   }

   if ( cpuIs8088or8086() )
   {
      printf("CPU is 8088/8086\n") ;
   }

   switch( atoi( argv[1] ) )
   {
      case 1:
         com = COM_1 ;
         break ;
      case 2:
         com = COM_2 ;
         break ;
      case 3:
         com = COM_3 ;
         break ;
      case 4:
         com = COM_4 ;
         break ;
      default:
         printf( "%s: invalid com port number\n", argv[ 1 ] ) ;
         return 0 ;
    }

   if( argc == 3 )
   {
      strcpy( cyclePath, argv [ 2 ] ) ;
      if ( strlen( cyclePath ) < 20 && cyclePath[ strlen( cyclePath - 1 ) ] != '\\')
      {
         strcat( cyclePath, "\\") ;
      }
   }
   makeCycleDirectory( cyclePath ) ;

   if (argc == 4 )
   {
       strcpy( hostAndPath, argv [ 3 ] ) ;
   }

   printf( "Starting and using serial port COM%s and cycle path %s\n", argv[1], cyclePath) ;

   if( ( rc = serial_open( com, 115200L, 8, 'n', 2, SER_HANDSHAKING_NONE ) ) != SER_SUCCESS )
   {
      printf( "Can't open port! (%s)\n", errors[ -rc ] ) ;
      return 0 ;
   }

   for(;;)
   {
      if ( kbhit() )
      {
         // End processing if we get a CTRL + C
         if ( getch() == 0x1b )
         {
            break ;
         }
      }
      if ( exitRequested() )
      {
         teardown() ;
         break ;
      }
      if( ( serial_read( com, &ch, 1 ) ) > 0 )
      {
         processNABU( ch, cyclePath, hostAndPath ) ;
      }
   }

   if( ( rc = serial_close( com ) ) != SER_SUCCESS )
   {
      printf( "Can't close serial port! (%s)\n", errors[ -rc ] ) ;
   }

   return 0 ;
}

// Write a block of data to the serial port
int WriteCommBlock( unsigned char* bByte, int nByteLen )
{
   serial_write( com, (const char*)bByte, nByteLen ) ;
   return 1 ;
}

//  Write a single byte to the serial port
int WriteCommByte( unsigned char bByte )
{
   serial_write( com, (const char*)&bByte, 1 ) ;
   return 1 ;
}

// If we have a loaded packet, free it, and reset the packet pointer and length
void freeLoadedPackets()
{
  if ( loadedPacketPtr != NULL )
  {
     free( loadedPacketPtr ) ;
  }
  loadedPacketPtr = NULL ;
  loadedPacketLength = 0 ;
}

// Calculates the CRC of a given cycle
void calculateCycleCRC( unsigned char *data, int dataLength )
{
   long seed = 0xFFFF ;
   int i ;

   for ( i = 0; i < dataLength; i++ )
   {
      int index = (int)(((((seed >> 8)) ^ (data[i] & 0xFF))) & 0xFF) ;
      seed <<= 8 ;
      seed ^= cycleCrcTable[ index ] ;
   }

   // ok, now get the high and low order CRC bytes
   seed ^= 0xFFFF ;
   data[ dataLength ] = (unsigned char) ((seed >> 8) & 0xFF) ;
   data[ dataLength + 1 ] = (unsigned char) (seed & 0xFF) ;
}

// Creates the time segment
void createTimeSegment()
{
   time_t now ;
   struct tm *currTime ;
   time(&now) ;
   currTime = localtime(&now) ;

   loadedPacketPtr  = ( unsigned char* )malloc( TIME_SEGMENT_SIZE ) ;

   loadedPacketPtr[ 0 ] = 0x7f ;
   loadedPacketPtr[ 1 ] = 0xff ;
   loadedPacketPtr[ 2 ] = 0xff ;
   loadedPacketPtr[ 3 ] = 0x0 ;
   loadedPacketPtr[ 4 ] = 0x0 ;
   loadedPacketPtr[ 5 ] = 0x7f ;
   loadedPacketPtr[ 6 ] = 0xff ;
   loadedPacketPtr[ 7 ] = 0xff ;
   loadedPacketPtr[ 8 ] = 0xff ;
   loadedPacketPtr[ 9 ] = 0x7f ;
   loadedPacketPtr[ 10 ] = 0x80 ;
   loadedPacketPtr[ 11 ] = 0x30 ;
   loadedPacketPtr[ 12 ] = 0x0 ;
   loadedPacketPtr[ 13 ] = 0x0 ;
   loadedPacketPtr[ 14 ] = 0x0 ;
   loadedPacketPtr[ 15 ] = 0x0 ;
   loadedPacketPtr[ 16 ] = 0x2 ;
   loadedPacketPtr[ 17 ] = 0x2 ;
   loadedPacketPtr[ 18 ] = currTime->tm_wday + 1 ;
   loadedPacketPtr[ 19 ] = 0x54 ;
   loadedPacketPtr[ 20 ] = currTime->tm_mon + 1 ;
   loadedPacketPtr[ 21 ] = currTime->tm_mday ;
   loadedPacketPtr[ 22 ] = currTime->tm_hour % 12 ;
   loadedPacketPtr[ 23 ] = currTime->tm_min ;
   loadedPacketPtr[ 24 ] = currTime->tm_sec ;
   loadedPacketPtr[ 25 ] = 0x0 ;
   loadedPacketPtr[ 26 ] = 0x0 ;

   // Calculate CRC will fill in indexes 27 and 28 with the CRC
   calculateCycleCRC( loadedPacketPtr, 27 ) ;
   loadedPacketLength = TIME_SEGMENT_SIZE ;
}

// Populates the packet header and CRC
void populatePacketHeaderAndCrc( long offset, unsigned char lastSegment, unsigned char *buffer, int bytesRead )
{
   unsigned char type = 0x20 ;

   // Cobble together the header
   buffer [ 0 ] = ((int) (segmentNumber >> 16) & 0xFF) ;
   buffer [ 1 ] = ((int) (segmentNumber >> 8) & 0xFF) ;
   buffer [ 2 ] = ((int) (segmentNumber & 0xFF)) ;
   buffer [ 3 ] = packetNumber ;

   // Owner
   buffer [ 4 ] = 0x1 ;

   // Tier
   buffer [ 5 ] = 0x7F ;
   buffer [ 6 ] = 0xFF ;
   buffer [ 7 ] = 0xFF ;
   buffer [ 8 ] = 0xFF ;

   // Mystery bytes
   buffer [ 9 ] = 0x7F ;
   buffer [ 10 ] = 0x80 ;

   // Packet Type
   if ( lastSegment )
   {
      // Set the 4th bit to mark end of segment
      type = (unsigned char) ( type | 0x10 ) ;
   }
   else if ( packetNumber == 0 )
   {
      type = 0xa1 ;
   }

   buffer [ 11 ] = type ;
   buffer [ 12 ] = packetNumber ;
   buffer [ 13 ] = 0x0 ;
   buffer [ 14 ] = ((int) (offset >> 8) & 0xFF) ;
   buffer [ 15 ] = ((int) (offset & 0xFF)) ;

   // Payload already prepopulated, so just calculate the CRC
   calculateCycleCRC( buffer, PACKET_HEADER_SIZE + bytesRead ) ;
}

// Create a file packet based on the current packet and segment number
int createFilePacket( char* filePath, char* hostAndPath, int tryDownload )
{
   char segmentName[ 100 ] ;
   FILE *file ;
   long fileSize = 0 ;
   int currentPacket = 0 ;
   long offset = 0 ;
   int bytesRead = 0 ;

   sprintf( segmentName, "%s%06lX.nab", filePath, segmentNumber ) ;
   file = fopen( segmentName, "rb" ) ;
   if ( file == NULL )
   {
      if ( tryDownload )
      {
         downloadFileViaHttp( filePath, hostAndPath, ".nabu", segmentNumber ) ;
         file = fopen( segmentName, "rb" ) ;
      }

      if ( file == NULL )
      {
         return 0 ;
      }
      else
      {
          printf( "Downloaded %s from %s\n", segmentName, hostAndPath );
      }
   }

   fseek( file, 0, SEEK_END ) ;
   fileSize = ftell( file ) ;
   rewind( file ) ;

   if ( fileSize > 0 )
   {
      while ( ftell( file ) < fileSize )
      {
         if ( currentPacket == packetNumber )
         {
            offset = ftell( file );
            loadedPacketPtr = ( unsigned char* )malloc( PACKET_HEADER_SIZE + PACKET_DATA_SIZE + PACKET_CRC_SIZE ) ;
            if ( loadedPacketPtr == NULL )
            {
               printf( "Error allocating memory\n" ) ;
               fclose( file ) ;
               return 0 ;
            }
            // Skip past the header and fill in the data
            bytesRead = fread( &loadedPacketPtr[ PACKET_HEADER_SIZE ], 1, PACKET_DATA_SIZE, file ) ;
            // Populate the header and CRC
            populatePacketHeaderAndCrc( offset, ftell( file ) == fileSize, loadedPacketPtr, bytesRead ) ;
            loadedPacketLength = PACKET_HEADER_SIZE + bytesRead + PACKET_CRC_SIZE ;
            fclose( file ) ;
            return 1 ;
         }
         else
         {
            fseek( file, PACKET_DATA_SIZE, SEEK_CUR ) ;
            currentPacket++ ;
         }
      }
      fclose( file ) ;
   }
   return 0;
}

// Load a file packet based on the current packet and segment number
int loadFilePacket( char* filePath, char* hostAndPath, int tryDownload )
{
   int packetLength = 0 ;
   char segmentName[ 100 ] ;
   FILE *file ;
   long fileSize = 0 ;
   int currentPacket = 0 ;
   unsigned char packetBuffer[ 2 ] ;

   sprintf( segmentName, "%s%06lX.pak", filePath, segmentNumber ) ;
   file = fopen( segmentName, "rb" ) ;
   if ( file == NULL )
   {
      if ( tryDownload )
      {
         downloadFileViaHttp( filePath, hostAndPath, ".pak", segmentNumber ) ;
         file = fopen( segmentName, "rb" ) ;
      }

      if ( file == NULL )
      {
         return 0 ;
      }
      else
      {
          printf( "Downloaded %s from %s\n", segmentName, hostAndPath );
      }
   }

   fseek( file, 0, SEEK_END ) ;
   fileSize = ftell(file) ;
   rewind(file) ;

   if ( fileSize > 0 )
   {
      while ( ftell( file ) + 2 < fileSize)
      {
         fread( packetBuffer, 1, 2, file ) ;
         packetLength = ( unsigned int )packetBuffer[ 1 ] ;
         packetLength = packetLength << 8 ;
         packetLength = packetLength + ( unsigned int )packetBuffer[ 0 ] ;

         if (currentPacket == packetNumber)
         {
            loadedPacketPtr  = ( unsigned char* )malloc( packetLength ) ;
            if ( loadedPacketPtr == NULL )
            {
               printf( "Error allocating memory\n" ) ;
               fclose( file ) ;
               return 0 ;
            }
            fread( loadedPacketPtr, 1, packetLength, file ) ;
            loadedPacketLength = packetLength ;
            fclose( file ) ;
            return 1 ;
         }
         else
         {
            fseek( file, packetLength, SEEK_CUR ) ;
         }
         currentPacket++ ;
      }
      fclose(file) ;
   }

   return 0 ;
}

// Add escape characters and send a packet
void sendPacket( )
{
   unsigned char *escapedPacket ;
   int escapedCharCount = 0 ;
   int i ;
   int counter = 0 ;

   for ( i = 0; i < loadedPacketLength; i++ )
   {
      if( loadedPacketPtr[ i ] == 0x10 )
      {
         escapedCharCount++ ;
      }
   }

   escapedPacket = ( unsigned char* ) malloc( loadedPacketLength + escapedCharCount ) ;

   for ( i = 0; i < loadedPacketLength; i++ )
   {
      if( loadedPacketPtr[ i ] == 0x10) {
          escapedPacket[ counter++ ] = 0x10 ;
      }
      escapedPacket[ counter++ ] = loadedPacketPtr[i] ;
   }

    WriteCommBlock( escapedPacket, loadedPacketLength+escapedCharCount ) ;
    free( escapedPacket ) ;
}

// Handle a file request
int handleFileRequest( unsigned char b, char* filePath, char* hostAndPath )
{
   unsigned char write[ 4 ] ;
   unsigned long tmp ;

   // Stage where we acknowledge the file request
   if ( processingStage == 0 )
   {
      write[ 0 ]= 0x10 ;
      write[ 1 ]= 0x6 ;
      WriteCommBlock(write, 2) ;
      processingStage = 1 ;
      return 1 ;
   }

   // Stage where we bring in the packet number
   if ( processingStage == 1 )
   {
      packetNumber = ( unsigned char )b ;
      processingStage = 2 ;
      return 1 ;
   }

   // Stage where we bring in first byte of segment number
   if ( processingStage == 2 )
   {
      segmentNumber = ( unsigned char )b ;
      processingStage = 3 ;
      return 1 ;
   }

   // Stage where we bring in second byte of segment number
   if ( processingStage == 3 )
   {
      tmp = ( unsigned char )b ;
      tmp = tmp << 8 ;
      segmentNumber = segmentNumber + tmp ;
      processingStage = 4 ;
      return 1 ;
   }

   // Stage where we bring in third byte of segment number
   if ( processingStage == 4 )
   {
      tmp = ( unsigned char )b ;
      tmp = tmp << 16 ;
      segmentNumber = segmentNumber + tmp ;
      printf( "Segment %06lX, Packet %06X \r\n", segmentNumber, packetNumber ) ;

      WriteCommByte(0xE4) ;

      freeLoadedPackets() ;

      if ( segmentNumber == 0x7fffffL )
      {
         createTimeSegment();
      }
      else if ( segmentNumber == 0x83 || packetNumber == 0x83 )
      {
         printf( "NABU reset detected\r\n" ) ;
         return 0 ;
      }
      // We will try local file access, then download.
      // Ugly code. Wow, this program is brand new and already needs a refactor.
      else if ( !loadFilePacket( filePath, hostAndPath, 0 ) )
      {
         if ( !createFilePacket( filePath, hostAndPath, 0 ) )
         {
            if ( !loadFilePacket( filePath, hostAndPath, 1 ) )
            {
               if ( !createFilePacket( filePath, hostAndPath, 1 ) )
               {
                  printf( "Could not load segment %06X and packet %06X\r\n", segmentNumber, packetNumber );
                  WriteCommByte( 0x90 ) ;
                  processingStage = 5;
                  return 1 ;
               }
            }
         }
      }

      WriteCommByte( 0x91 ) ;
      processingStage = 7 ;
      return 1 ;
   }

   // Stage where we absorb byte 1 for "packet not found"
   if ( processingStage == 5 )
   {
      if ( b != 0x10 )
      {
         return 0 ;
      }
      processingStage = 6 ;
      return 1 ;
   }

   // Stage where we absorb byte 2 for "packet not found"
   if ( processingStage == 6 )
   {
      return 0 ;
   }

   // Stage where we respond after acknowledging that we have a packet
   if ( processingStage == 7 )
   {
      if ( b != 0x10)
      {
         write[ 0 ] = 0x10 ;
         write[ 1 ] = 0x6 ;
         write[ 2 ] = 0xE4 ;
         WriteCommBlock( write, 3 ) ;
         return 0 ;
      }

      processingStage = 8 ;
      return 1 ;
   }

   // Stage where we abosrb and check a byte
   if ( processingStage == 8 )
   {
      if ( b != 0x6 ) {
          return 0 ;
      }

      // We will assume success
      sendPacket( ) ;

      write[ 0 ] = 0x10 ;
      write[ 1 ] = 0xE1 ;
      WriteCommBlock( write, 2 ) ;
   }

   return 0 ;
}

// Reset the NABU state machine
int resetNabuState( )
{
   processingByteInitialized = 0 ;
   processingStage = 0 ;
   freeLoadedPackets() ;
   return 1 ;
}

// Main NABU processing loop
void processNABU( unsigned char b, char* filePath, char* hostAndPath )
{
   int channel ;
   unsigned char write[ 3 ];
   unsigned char switchingByte = b ;

   if ( processingByteInitialized )
   {
      switchingByte = processingByte ;
   }
   else
   {
      lastResetProcessingByte = processingByte;
      processingByte = b ;
      processingByteInitialized = 1 ;
   }

   switch (switchingByte)
   {
      case 0x85: // Channel
         if ( processingStage == 0 )
         {
            write[ 0 ] = 0x10 ;
            write[ 1 ] = 0x6 ;
            WriteCommBlock( write, 2 ) ;
            processingStage = 1 ;
            break ;
         }

         if ( processingStage == 1 )
         {
            channel = b ;
            processingStage = 2 ;
            break ;
         }
         if ( processingStage == 2 )
         {
            channel = channel + ( ( ( unsigned int )b ) << 8 ) ;
            printf( "Channel: %d\r\n", channel ) ;
            WriteCommByte( 0xE4 ) ;
         }
         resetNabuState() ;
         break ;
      case 0x84: // File Transfer
         if ( processingStage == 0 )
         {
            printf( "File Request: " ) ;
         }
         if ( handleFileRequest(b, filePath, hostAndPath ) )
         {
            break ;
         }
         resetNabuState() ;
         break ;
      case 0x83:
         write[ 0 ] = 0x10 ;
         write[ 1 ] = 0x6 ;
         write[ 2 ] = 0xE4 ;
         WriteCommBlock( write, 3 ) ;
         resetNabuState() ;
         break ;
      case 0x82:
         if ( processingStage == 0 )
         {
            printf( "Configure Channel\r\n" ) ;
            write[ 0 ] = 0x10 ;
            write[ 1 ] = 0x6 ;
            WriteCommBlock( write, 2 ) ;
            processingStage = 1;
            break;
         }

         if ( processingStage == 1 )
         {
            write[ 0 ] = 0x1F ;
            write[ 1 ] = 0x10 ;
            write[ 2 ] = 0xE1 ;
            WriteCommBlock( write, 3 ) ;
         }

         resetNabuState() ;
         break ;
      case 0x81:
         if ( processingStage == 0 )
         {
            write[ 0 ] = 0x10 ;
            write[ 1 ] = 0x06 ;
            WriteCommBlock( write, 2 ) ;
            processingStage = 1 ;
            break ;
         }
         if ( processingStage == 1 )
         {
            processingStage = 2 ;
            break ;
         }
         if (processingStage == 2)
         {
            WriteCommByte( 0xE4 ) ;
         }
         resetNabuState() ;
         break ;
      case 0x1E:
         WriteCommByte( 0x10 ) ;
         WriteCommByte( 0xE1 ) ;
         resetNabuState() ;
         break ;
      case 0x5:
         WriteCommByte( 0xE4) ;
         resetNabuState() ;
         break ;
      case 0xF:
         resetNabuState() ;
         break ;
      default:
         printf( "Unrecognized command 0x%X\r\n", b ) ;
         resetNabuState();

         // Let's try to execute the last inititialized command we had
         processNABU( lastResetProcessingByte, filePath, hostAndPath);
         break ;
   }
}

