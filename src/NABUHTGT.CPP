/*
   NABU HTGet
   Created by Chris Lenderman

   Based off of mTCP HTGet.cpp
   Copyright (C) 2011-2024 Michael B. Brutman (mbbrutman@gmail.com)
   mTCP web page: http://www.brutman.com/mTCP


   HTGet.cpp is part of mTCP.

   mTCP is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   mTCP is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with mTCP.  If not, see <http://www.gnu.org/licenses/>.


   Description: NABUGet for DOS, used for downloading NABU segment
   from the Internet

   Changes:

   2025-01-07: Initial version

*/


#include <io.h>
#include <fcntl.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/utime.h>
#include <unistd.h>

#include "types.h"

#include "timer.h"
#include "trace.h"
#include "utils.h"
#include "packet.h"
#include "arp.h"
#include "tcp.h"
#include "tcpsockm.h"
#include "udp.h"
#include "dns.h"


#define HOSTNAME_LEN        (80)
#define PATH_LEN           (256)

#define TCP_RECV_BUFFER  (16384)
#define INBUFSIZE         (8192)
#define LINEBUFSIZE        (512)
#define SOCK_PRINTF_SIZE  (1024)

#define CONNECT_TIMEOUT  (10000ul)


bool     Verbose = false;
bool     QuietMode = true;


// Globals filled in as a result of an HTTP response
bool     TransferEncoding_Chunked = false;
bool     ExpectedContentLengthSent = false;
uint32_t ExpectedContentLength = 0;
uint16_t HttpResponse = 500;
bool     CurrentlyProcessing = false;
bool     mTcpInitialized = false;

// Server and file information
char Hostname[ HOSTNAME_LEN ];
char Path[ PATH_LEN ];

IpAddr_t HostAddr;
uint16_t ServerPort = 80;

TcpSocket *sock;


// Buffers
char lineBuffer[ LINEBUFSIZE ];

uint8_t  *inBuf;                 // Input buffer
uint16_t  inBufStartIndex = 0;   // First unconsumed char in inBuf
uint16_t  inBufLen=0;            // Index to next char to fill

// Return code table
//
// If we get a specific HTTP return code we can map it to a program
// return code with this table.
//
// In general, a return code 0 means "good communications but unrecognized
// HTTP response code".  A return code 1 is some form of hard error.  Anything
// else that is interesting should be described by this table.


typedef struct {
  uint16_t httpCodeStart;
  uint16_t httpCodeEnd;
  uint8_t  dosRc;
  uint8_t  reserved;
} ReturnCodeRec_t;


ReturnCodeRec_t rcMappingTable[] = {

  { 100, 199, 10 },  // Default for 100 to 199 if not mapped out

  { 200, 299, 20 },  // Default for 200 to 299 if not mapped out
  { 200, 200, 21 },  // OK
  { 201, 201, 22 },  // Created
  { 202, 202, 23 },  // Accepted
  { 203, 203, 24 },  // Non-Authoritative Information
  { 204, 204, 25 },  // No Content
  { 205, 205, 26 },  // Reset Content
  { 206, 206, 27 },  // Partial Content

  { 300, 399, 30 },  // Default for 300 to 399 if not mapped out
  { 300, 300, 31 },  // Multiple Choices
  { 301, 301, 32 },  // Moved Permanently
  { 302, 302, 33 },  // Found
  { 303, 303, 34 },  // See Other
  { 304, 304, 35 },  // Not Modified
  { 305, 305, 36 },  // Use Proxy
  { 307, 307, 37 },  // Temporary Redirect

  { 400, 499, 40 },  // Default for 400 to 499 if not mapped out
  { 400, 400, 41 },  // Bad Request
  { 401, 401, 42 },  // Unauthorized
  { 402, 402, 43 },  // Payment Required
  { 403, 403, 44 },  // Forbidden
  { 404, 404, 45 },  // Not Found
  { 410, 410, 46 },  // Gone

  { 500, 599, 50 },  // Default for 500 to 599 if not mapped out
  { 500, 500, 51 },  // Internal Server Error
  { 501, 501, 52 },  // Not Implemented
  { 503, 503, 53 },  // Service Unavailable
  { 505, 505, 54 },  // HTTP Version Not Supported
  { 509, 509, 55 },  // Bandwidth Limit Exceeded

};


uint8_t mapResponseCode( uint16_t httpRc ) {

  uint8_t rc = 0;

  for ( uint8_t i = 0; i < sizeof(rcMappingTable)/sizeof(ReturnCodeRec_t); i++ ) {

    if ( httpRc >= rcMappingTable[i].httpCodeStart && httpRc <= rcMappingTable[i].httpCodeEnd ) {
      rc = rcMappingTable[i].dosRc;
      if ( httpRc == rcMappingTable[i].httpCodeStart && httpRc == rcMappingTable[i].httpCodeEnd ) {
        // Found our exact code - no point in scanning the rest of the table
        break;
      }
    }

  }

  return rc;
}



// Error and Verbose message handling
//
// Yes, these are very similar ...

inline void errorMessage( char *fmt, ... ) {
  if ( !QuietMode ) {
    va_list ap;
    va_start( ap, fmt );
    vfprintf( stderr, fmt, ap );
    va_end( ap );
  }
}

inline void verboseMessage( char *fmt, ... ) {
  if ( Verbose ) {
    va_list ap;
    va_start( ap, fmt );
    vfprintf( stderr, fmt, ap );
    va_end( ap );
  }
}



// Ctrl-Break and Ctrl-C handler.  Check the flag once in a while to see if
// the user wants out.

volatile uint8_t CtrlBreakDetected = 0;

void __interrupt __far ctrlBreakHandler( ) {
  CtrlBreakDetected = 1;
}

uint8_t userWantsOut( void ) {

  if ( CtrlBreakDetected ) {
    errorMessage( "Ctrl-Break detected - aborting!\n" );
    return 1;
  }

  if ( biosIsKeyReady( ) ) {
    char c = biosKeyRead( );
    if ( (c == 27) || (c == 3) ) {
      errorMessage( "Esc or Ctrl-C detected - aborting!\n");
      return 1;
    }
  }

  return 0;
}


bool exitRequested() {
  return CtrlBreakDetected && !CurrentlyProcessing;
}


// Ends the TCP/IP stack.
// Use this after TCP/IP has been successfully initialized.

static void shutdown( int rc ) {
  verboseMessage( "DOS errorlevel code: %d\n", rc );
  Utils::endStack( );
}

enum StopCode {
  NotDone,
  UserBreak,
  FileError,
  SocketError,
  SocketClosed,
  ProtocolError,
  AllDoneAndGood
};

char *StopCodeStrings[] = {
  "Not Done",
  "User Break",
  "File Error",
  "Socket Error",
  "Socket Closed",
  "Protocol Error",
  "All Finished"
};



// drainAndCloseSocket
//
// Uses inBuf and will overwrite anything in it, so make sure you are
// totally done with it.

void drainAndCloseSocket( void ) {

  // Drain socket for a reasonable amount of time before closing

  verboseMessage( "Closing socket\n" );

  clockTicks_t start = TIMER_GET_CURRENT( );

  uint32_t bytesRead = 0;

  while ( 1 ) {

    PACKET_PROCESS_MULT( 5 );
    Tcp::drivePackets( );
    Arp::driveArp( );

    int16_t rc = sock->recv( inBuf, INBUFSIZE );
    if ( rc > 0 ) bytesRead += rc;

    if ( sock->isRemoteClosed( ) || (Timer_diff( start, TIMER_GET_CURRENT( ) ) > TIMER_MS_TO_TICKS( 5000 )) ) {
      break;
    }

  }

  verboseMessage( "%lu bytes read while draining the socket\n", bytesRead );

  sock->close( );
  TcpSocketMgr::freeSocket(sock);
  sock = NULL;
}



// fillInBuf
//
// Fill inBuf to the max or until no data is available from the socket.
// inBuf will be compacted if needed.

StopCode fillInBuf( void ) {

  StopCode rc = NotDone;

  // Compact inBuf first if needed

  if ( inBufLen == 0 ) {
    // If everything has been consumed it's safe and cheap to reset.
    inBufStartIndex = 0;
  } else if ( inBufStartIndex + LINEBUFSIZE + 128 > INBUFSIZE ) {
    // Need room for one maximum length header line.  If we don't have
    // that then compact to make room for one.
    memmove( inBuf, inBuf+inBufStartIndex, inBufLen );
    inBufStartIndex = 0;
  }

  // Could add a third case for compacting: inBufStartIndex + inBufLen = INBUFSIZE.
  // But assuming reasonable values for INBUFSIZE and LINEBUFSIZE this won't
  // help you very much.


  uint16_t bytesToRead = INBUFSIZE - (inBufStartIndex + inBufLen);

  TRACE(( "HTGET: fillInBuf start: inBufStartIndex=%u, inBufLen=%u\n",
          inBufStartIndex, inBufLen ));


  while ( (rc == NotDone) && bytesToRead ) {

    if ( userWantsOut( ) ) {
      rc = UserBreak;
      break;
    }

    // Service the connection
    PACKET_PROCESS_MULT( 5 );
    Arp::driveArp( );
    Tcp::drivePackets( );

    int16_t recvRc = sock->recv( inBuf + inBufStartIndex + inBufLen, bytesToRead );

    if ( recvRc > 0 ) {

      // Some bytes read.  Keep going
      inBufLen += recvRc;
      bytesToRead -= recvRc;

    } else if ( recvRc < 0 ) {

      rc = SocketError;

    } else if ( recvRc == 0 ) {

      // Nothing read.  Could be just nothing available, or it could
      // be a closed socket.

      if ( sock->isRemoteClosed( ) ) break;

    }

  } // end while

  TRACE(( "HTGET: fillInBuf end: inBufStartIndex=%u, inBufLen=%u, rc=%u\n",
          inBufStartIndex, inBufLen, rc ));

  return rc;
}



// If there is a full line of input in the input buffer:
//
// - return a copy of the line in target
// - adjust the input buffer to remove the line
//
// Removing a full line of input and sliding the remaining buffer down
// is slow, but makes the buffer code easier.
//
// Note that this code does not search indefinitely.  You have to have
// a CR/LF within the first LINEBUFSIZE bytes and the output buffer should be
// LINEBUFSIZE bytes too.  If you violate this you will probably hang the
// program up.  No HTTP header is coming back that large though.

uint16_t getLineFromInBuf( char *target ) {

  if ( inBufLen == 0 ) return 1;

  for ( int i=inBufStartIndex,len=0; len < (inBufLen-1); i++,len++ ) {

    if ( inBuf[i] == '\r' && inBuf[i+1] == '\n' ) {

      // Found delimiter

      int bytesToCopy = len;
      if ( bytesToCopy > (LINEBUFSIZE-1) ) {
        bytesToCopy = LINEBUFSIZE-1;
        errorMessage( "Warning: Long header truncated. (Was %u bytes long.)\n", len );
      }

      memcpy( target, inBuf+inBufStartIndex, bytesToCopy );
      target[bytesToCopy] = 0;

      inBufLen -= len + 2;    // Adjust buffer length.
      inBufStartIndex = i+2;  // Adjust buffer start.

      TRACE(( "HTGET: Header line: %s\n", target ));
      return 0;
    }

  }


  if ( inBufLen > LINEBUFSIZE  ) {
    // There should have been enough data to read a header.
    // Wipe out the inBuf and see what happens.
    inBufStartIndex = inBufLen = 0;
    errorMessage( "Could not find the end of a header; clearing the buffer\n" );
  }

  // Not yet
  return 1;
}




// sock_getline
//
// Read lines from the socket that are terminated with a CR/LF.  If a
// full line is not available yet then buffer the partial contents.
// If we don't get a line in a reasonable amount of time then time out
// and return, which is probably fatal to the app.
//
// Returns 0 if successful, -1 if error

int sock_getline( char *buffer) {

  // Have previous data to check already?
  if ( getLineFromInBuf( buffer ) == 0 ) return 0;

  clockTicks_t start = TIMER_GET_CURRENT( );

  while ( 1 ) {

    if (Timer_diff( start, TIMER_GET_CURRENT( ) ) > TIMER_MS_TO_TICKS( CONNECT_TIMEOUT ) ) {
      errorMessage( "Timeout reading from socket\n" );
      return -1;
    }

    StopCode rc = fillInBuf( );
    if ( rc != NotDone) return -1;

    if ( getLineFromInBuf( buffer ) == 0 ) break;

  }

  // Good return!
  return 0;
}




// sock_printf
//
// This will loop until it can push all of the data out.
// Does not check the incoming data length, so don't flood it.
// (The extra data will be ignored/truncated ...)
//
// Returns 0 on success, -1 on error


static char spb[ SOCK_PRINTF_SIZE ];

static int sock_printf( char *fmt, ... ) {

  va_list ap;
  va_start( ap, fmt );
  int vsrc = vsnprintf( spb, SOCK_PRINTF_SIZE, fmt, ap );
  va_end( ap );

  if ( (vsrc < 0) || (vsrc >= SOCK_PRINTF_SIZE) ) {
    errorMessage( "Formatting error in sock_printf\n" );
    return -1;
  }

  uint16_t bytesToSend = vsrc;
  uint16_t bytesSent = 0;

  while ( bytesSent < bytesToSend ) {

    // Process packets here in case we have tied up the outgoing buffers.
    // This will give us a chance to push them out and free them up.

    PACKET_PROCESS_MULT(5);
    Arp::driveArp( );
    Tcp::drivePackets( );

    int rc = sock->send( (uint8_t *)(spb+bytesSent), bytesToSend-bytesSent );
    if (rc > 0) {
      bytesSent += rc;
    }
    else if ( rc == 0 ) {
      // Out of send buffers maybe?  Loop around to process packets
    }
    else {
      return -1;
    }

  }

  return 0;
}




int8_t resolve( char *ServerAddrName, IpAddr_t &serverAddr ) {

  int8_t rc = Dns::resolve( ServerAddrName, serverAddr, 1 );
  if ( rc < 0 ) return -1;

  uint8_t done = 0;

  while ( !done ) {

    if ( userWantsOut( ) ) break;

    if ( !Dns::isQueryPending( ) ) break;

    PACKET_PROCESS_MULT(5);
    Arp::driveArp( );
    Tcp::drivePackets( );
    Dns::drivePendingQuery( );

  }

  // Query is no longer pending or we bailed out of the loop.
  rc = Dns::resolve( ServerAddrName, serverAddr, 0 );


  if ( rc != 0 ) {
    errorMessage( "Error resolving %s\n", Hostname );
    return -1;
  }

  verboseMessage( "Hostname %s resolved to %d.%d.%d.%d\n",
                  Hostname,
                  serverAddr[0], serverAddr[1],
                  serverAddr[2], serverAddr[3] );

  return 0;
}



int8_t connectSocket( void ) {

  uint16_t localport = 2048 + rand( );

  sock = TcpSocketMgr::getSocket( );
  if ( sock->setRecvBuffer( TCP_RECV_BUFFER ) ) {
    errorMessage( "Error creating socket\n" );
    return -1;
  }

  if ( sock->connectNonBlocking( localport, HostAddr, ServerPort ) ) return -1;

  int8_t rc = -1;

  clockTicks_t start;
  clockTicks_t lastCheck;
  start = lastCheck = TIMER_GET_CURRENT( );

  while ( 1 ) {

    if ( userWantsOut( ) ) break;

    PACKET_PROCESS_MULT(5);
    Tcp::drivePackets( );
    Arp::driveArp( );

    if ( sock->isConnectComplete( ) ) {
      rc = 0;
      break;
    }

    if ( sock->isClosed( ) || (Timer_diff( start, TIMER_GET_CURRENT( ) ) > TIMER_MS_TO_TICKS( CONNECT_TIMEOUT )) ) {
      break;
    }

    // Sleep so that we are not spewing TRACE records.
    while ( lastCheck == TIMER_GET_CURRENT( ) ) { };
    lastCheck = TIMER_GET_CURRENT( );

  }

  if ( rc == 0 ) {
    verboseMessage( "Connected using local port %u!\n", localport );
  }
  else {
    errorMessage( "Connection failed!\n" );
  }

  return rc;
}


// sendHeaders
//
// Returns 0 if all went well, -1 if an error occurs

int sendHeaders( void ) {

  int rc;


  verboseMessage( "Sending HTTP 1.1 request\n");
  rc = sock_printf( "GET %s HTTP/1.1\r\n"
                        "User-Agent: mTCP HTGet " __DATE__ "\r\n"
                        "Host: %s\r\n"
                        "Connection: close\r\n",
                        Path,
                        Hostname );

  rc = sock_printf( "\r\n" );
  if ( rc ) return -1;

  return 0;
}


// readHeaders
//
// Returns 0 if we can read everything successfully, -1 if not.
// Note that even reading a bad HTTP return code is success as
// far as we are concerned; we are only reporting socket and
// parsing errors.
//
// As a side-effect HttpReponse will be set with the numeric
// code we get from the server.

int8_t readHeaders( void ) {

  uint16_t response;

  // Get and parse the first line (version and response code)

  if ( sock_getline( lineBuffer ) ) {
    return -1;
  }

  if ( (strncmp(lineBuffer, "HTTP/1.1", 8) != 0) ) {
    errorMessage( "Not an HTTP 1.1 server\n" );
    return -1;
  }

  // Skip past HTTP version number
  char *s = lineBuffer + 8;
  char *s2 = s;

  // Skip past whitespace
  while ( *s ) {
    if ( *s != ' ' && *s !='\t' ) break;
    s++;
  }

  if ( (s == s2) || (*s == 0) || (sscanf(s, "%3d", &response) != 1) ) {
    errorMessage( "Malformed HTTP version line\n" );
    return -1;
  }

  HttpResponse = response;

  while ( sock_getline( lineBuffer ) == 0 ) {

    if ( *lineBuffer == 0 ) break;


    if ( strnicmp( lineBuffer, "Content-Length:", 15 ) == 0) {
      // Skip past Content-Length:
      s = lineBuffer + 15;
      ExpectedContentLength = atol(s);
      ExpectedContentLengthSent = true;
    }
    else if (stricmp(lineBuffer, "Transfer-Encoding: chunked") == 0) {
      TransferEncoding_Chunked = true;
    }
  }

  if ( ExpectedContentLengthSent ) {
    verboseMessage( "Expected content length: %lu\n", ExpectedContentLength );
    if ( ExpectedContentLength == 0 )
    {
      return -1;
    }
  }
  else {
    verboseMessage( "No content length header sent\n" );
  }

  if ( TransferEncoding_Chunked ) {
    verboseMessage( "Chunked transfer encoding being used\n" );
  }

  if ( response == 404 ) {
    return -1;
  }

  return 0;
}


void fileWriteError( int localErrno ) {
  errorMessage( "File write error: %s\n", strerror(localErrno) );
}


int fileWriter( uint8_t *buffer, uint16_t bufferLen, FILE *outputFile ) {
  // Remember, fwrite only fails this check if there is an error.
  if ( fwrite( buffer, 1, bufferLen, outputFile ) != bufferLen ) {
    int localErrno = errno;
    fileWriteError( localErrno );
    return 1;
  }
  return 0;
}


// Return:
//   n: a number that we parsed and another that says how many chars we consumed.
//  -1: Not enough chars; come back with a bigger buffer
//  -2: Hard error; we could not parse this.

int32_t getChunkSize( uint8_t *buffer, uint16_t bufferLen, uint16_t *bytesConsumed ) {

  TRACE(( "HTGET: getChunkSize: Start bufferLen %d\n", bufferLen ));

  int32_t rc = -2;

  // Scan what we have to see if we can parse the entire thing.
  int i=0;
  while ( i < bufferLen ) {
    if ( isdigit(buffer[i]) || (buffer[i] >= 'A' && buffer[i] <= 'F') ||  (buffer[i] >= 'a' && buffer[i] <= 'f') ) {
      i++;
    } else {
      break;
    }
  }

  if ( i == bufferLen ) return -1;
  if ( i > 6 ) return -2; // Six hex digits ... f-ck off.

  if ( buffer[i] == ';' ) {
    // Great ..  we have a chunk extension.  Ignore it.
    while ( i < bufferLen ) {
      if ( buffer[i] != '\r' ) i++; else break;
    }
    if ( i == bufferLen ) {
      return -1;
    }
  }
  else if ( buffer[i] != '\r' ) {
    return -2;
  }


  // At this point we are safely sitting on a carriage return, but
  // we need both a carriage return and a line fed.

  i++;
  if ( i == bufferLen ) return -1;     // Need more chars
  if ( buffer[i] != '\n' ) return -2;  // Parse error

  i++; // Consume the \n

  // All good.  Parse the hex

  if ( sscanf( (char *)buffer, "%lx", &rc ) != 1 ) {
    return -2;
  }

  TRACE(( "HTGET: getChunkSize: bytes consumed = %d\n", i ));

  *bytesConsumed = i;
  return rc;
}


int readContent( char* outputFilename ) {

  // We were explicitly told to expect content or were not told but were not reading
  // just headers, so something might come.

  verboseMessage( "Receiving content\n" );

  // Open output file now if specified.  If not valid, return.

  FILE *outputFile = fopen( outputFilename, "wb" );
  if ( outputFile == NULL ) {
    fileWriteError( errno );
    return -1;
  }

  // By this point:
  // - outputFile points to a file.
  // - inBuf has some leftover bytes from it.



  uint32_t TotalBytesReceived = 0;  // Actually, just content bytes.

  StopCode stopCode = NotDone;

  while ( stopCode == NotDone ) {

    TRACE(( "HTGET: recv content loop: inBufStartIndex=%u inBufLen=%u\n",
            inBufStartIndex, inBufLen ));

    int32_t nextChunkSize;

    if ( TransferEncoding_Chunked ) {

      TRACE(( "HTGET: gettingNextChunkSize\n" ));

      uint16_t bytesConsumed = 0;
      nextChunkSize = getChunkSize( inBuf + inBufStartIndex, inBufLen, &bytesConsumed );

      while ( nextChunkSize == -1 ) {

        TRACE(( "Not enough bytes to read chunk size." ));

        stopCode = fillInBuf( );
        if ( stopCode != NotDone ) break;

        nextChunkSize = getChunkSize( inBuf, inBufLen, &bytesConsumed );

        // We've tried to read the socket for more data.  If you still don't have enough
        // data and the socket is closed, you are done.
        if ( (nextChunkSize == -1) && sock->isRemoteClosed( ) ) {
          stopCode = ProtocolError;
          break;
        }

      } // end while

      TRACE(( "HTGET: nextChunkSize=%ld, stopCode=%u\n", nextChunkSize, stopCode ));

      if ( stopCode == NotDone ) {

        // By this point we have a next chunk size or a parse error.

        if ( nextChunkSize == 0 ) {
          stopCode = AllDoneAndGood;
        } else if ( nextChunkSize == -2 ) {
          stopCode = ProtocolError;
        } else {
          inBufStartIndex += bytesConsumed;
          inBufLen -= bytesConsumed;
        }

      }

    } else {
      TRACE(( "HTGET: Not using chunked transfers, nextChunkSize set to a large value\n" ));
      nextChunkSize = INBUFSIZE;
    }


    if ( stopCode != NotDone ) break;


    while ( nextChunkSize && (stopCode == NotDone) ) {

      TRACE(( "HTGET: File write loop: nextChunkSize=%lu, inBufStartIndex=%u, inBufLen=%u\n",
              nextChunkSize, inBufStartIndex, inBufLen ));

      if ( userWantsOut( ) ) {
        stopCode = UserBreak;
        break;
      }

      // If inBuf is empty fill it.  Reading more than we need for this chunk is fine.

      if ( inBufLen == 0 ) {

        stopCode = fillInBuf( );

        // Just tried to read the socket.  If we did not get any data
        // and the socket is closed then we will not get any data.

        if ( (inBufLen == 0) && sock->isRemoteClosed( ) ) {
          if ( TransferEncoding_Chunked ) {
            stopCode = ProtocolError;
          } else {
            stopCode = AllDoneAndGood;
          }
          break;
        }

      }

      if ( stopCode != NotDone ) break;

      // Write whatever is in inBuf.

      uint16_t bytesToWrite = nextChunkSize;
      if ( bytesToWrite > inBufLen ) bytesToWrite = inBufLen;

      TRACE(( "HTGET: before write: inBufStartIndex=%u, bytesToWrite=%u\n", inBufStartIndex, inBufLen ));

      if ( fileWriter( inBuf + inBufStartIndex, bytesToWrite, outputFile ) ) {
        stopCode = FileError;
        break;
      }

      TotalBytesReceived += bytesToWrite;
      nextChunkSize = nextChunkSize - bytesToWrite;
      inBufLen = inBufLen - bytesToWrite;
      inBufStartIndex += bytesToWrite;

    } // end while


    if ( TransferEncoding_Chunked ) {

      // We finished reading a chunk.  There should be a CR/LF pair after the
      // chunk.

      // If we don't have enough bytes then read some more.  If there are less
      // than two bytes available and the socket closes then this is a protocol
      // error.

      while ( inBufLen < 2 ) {

        stopCode = fillInBuf( );
        if ( stopCode != NotDone ) break;

        if ( (inBufLen < 2 ) && sock->isRemoteClosed( ) ) {
          stopCode = ProtocolError;
          break;
        }

      }

      if ( inBuf[inBufStartIndex] == '\r' && inBuf[inBufStartIndex+1] == '\n' ) {
        inBufStartIndex += 2;
        inBufLen -= 2;
        TRACE(( "HTGET: Read trailing CR LF at end of chunk\n" ));
      } else {
        stopCode = ProtocolError;
        TRACE(( "HTGET: Looking for CR LF, found %u and %u\n",
                inBuf[inBufStartIndex], inBuf[inBufStartIndex+1] ));
      }

    }

  } // end big while


  verboseMessage( "Receive content exit: %s\n", StopCodeStrings[stopCode] );

  if ( fclose( outputFile ) ) {
    int localErrno = errno;
    fileWriteError( localErrno );
    return -1;
  }

  int rc = -1;

  if ( stopCode == AllDoneAndGood ) {
    if ( !ExpectedContentLengthSent || (ExpectedContentLength == TotalBytesReceived) ) {
      rc = 0;
    } else {
      errorMessage( "Warning: expected %lu bytes, received %lu bytes\n", ExpectedContentLength, TotalBytesReceived );
    }
  }

  verboseMessage( "Received %lu bytes\n", TotalBytesReceived );


  return rc;
}

static int parseUrl( char* hostnameAndPath, char* fileNameExtension, unsigned long segmentNumber ) {

    char fileName[200];
    sprintf(fileName, "/%06lX%s", segmentNumber, fileNameExtension);

    char *pathStart = strchr( hostnameAndPath, '/' );
    if ( pathStart == NULL ) {

      strncpy( Hostname, hostnameAndPath, HOSTNAME_LEN );
      Hostname[ HOSTNAME_LEN - 1 ] = 0;

      Path[0] = '/';
      Path[1] = 0;
      strncat(Path, fileName, PATH_LEN - strlen(Path));
    }
    else {

      strncpy( Hostname, hostnameAndPath, pathStart - hostnameAndPath );
      Hostname[ HOSTNAME_LEN - 1 ] = 0;

      strncpy( Path, pathStart, PATH_LEN );
      Path[ PATH_LEN - 1 ] = 0;
      strncat(Path, fileName, PATH_LEN - strlen(Path));
    }


    char *portStart = strchr( Hostname, ':' );

    if ( portStart != NULL ) {
      ServerPort = atoi( portStart+1 );
      if ( ServerPort == 0 ) {
        return 0;
      }

      // Truncate hostname early
      *portStart = 0;
    }
  return 1;

}


int initialize() {

  TransferEncoding_Chunked = false;
  ExpectedContentLengthSent = false;
  ExpectedContentLength = 0;
  HttpResponse = 500;
  ServerPort = 80;
  inBufLen = 0;
  inBufStartIndex = 0;
  CurrentlyProcessing = 0;

  if ( !mTcpInitialized ) {
    if ( Utils::parseEnv( ) != 0 ) {
      errorMessage( "Could not parse environment\n" );
      return 0;
    }

    if ( Utils::initStack( 1, TCP_SOCKET_RING_SIZE, ctrlBreakHandler, ctrlBreakHandler ) ) {
      errorMessage( "Error: Could not init TCP\n" );
      return 0;
    }
    mTcpInitialized = 1;
  }
  return 1;
}

void teardown() {

  if ( mTcpInitialized ) {
    mTcpInitialized = 0;
    shutdown(1);
    verboseMessage( "Teardown complete\n" );
  }
}

int downloadFileViaHttp(char* filePath, char* hostAndPath, char* fileNameExtension, unsigned long segmentNumber ) {

  if (userWantsOut()) {
    return 0;
  }

  if (!initialize()) {
    errorMessage( "Could not initialize\n" );
    return 0;
  }

  verboseMessage( "Initialized\n" );

  CurrentlyProcessing = 1;

  if ( !parseUrl( hostAndPath, fileNameExtension, segmentNumber ) ) {
    errorMessage( "Could not parse passed in URL\n" );
    CurrentlyProcessing = 0;
    return 0;
  }

  inBuf = (uint8_t *)malloc( INBUFSIZE );
  if ( !inBuf ) {
    errorMessage( "Error: Could not allocate memory\n" );
    CurrentlyProcessing = 0;
    return 0;
  }

  if ( resolve(Hostname, HostAddr) ) {
    errorMessage( "Error: Could not resolve hostname\n" );
    free( inBuf );
    CurrentlyProcessing = 0;
    return 0;
  }

  if ( connectSocket( ) ) {
    errorMessage( "Error: Could not connect to socket\n" );
    free( inBuf );
    CurrentlyProcessing = 0;
    return 0;
  }

  if ( sendHeaders( ) ) {
    errorMessage( "Error: Could not send headers\n" );
    drainAndCloseSocket();
    free( inBuf );
    CurrentlyProcessing = 0;
    return 0;
  }

  if ( readHeaders( ) ) {
    verboseMessage( "Zero length headers\n" );
    drainAndCloseSocket();
    free( inBuf );
    CurrentlyProcessing = 0;
    return 0;
  }

  int rc;

  if ( (ExpectedContentLengthSent && (ExpectedContentLength == 0)) ) {
    // Do nothing if we already have an up to date copy of the content.
    verboseMessage( "No content expected so none read\n" );
    rc = 0;
  }
  else {
    char fileNameCorrectedExtension[ 5 ];
    char fileName [ 100 ];

    // DOS only supports 8.3 file names, truncate the extension if needed
    strncpy( fileNameCorrectedExtension, fileNameExtension, 4 );
    fileNameCorrectedExtension[ 4 ] = 0;
    sprintf( fileName, "%s%06lX%s", filePath, segmentNumber, fileNameCorrectedExtension );
    verboseMessage( "Reading content to filename %s\n", fileName );
    rc = readContent( fileName );
  }

  drainAndCloseSocket( );

  if ( rc == 0 ) {
    rc = mapResponseCode( HttpResponse );
  }
  else {
    rc = 1;
  }

  free( inBuf );
  CurrentlyProcessing = 0;
  return 1;
}
