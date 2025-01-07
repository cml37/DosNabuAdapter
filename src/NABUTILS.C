//---------------------------------------------------------------------------
//
//  Module: utils.c
//
//  Purpose:
//     Environment Utilities
//
//  Development Team:
//     Chris Lenderman
//
//  History:   Date       Author      Comment
//             12/29/24   ChrisL      Wrote it.
//
//---------------------------------------------------------------------------

#include "nabutils.h"

// Detects the video mode.
int detectVideoMode()
{
   union REGS regs ;
   char far *ptr ;

   // VGA only enable video
   regs.w.ax = 0x1200 ;
   regs.h.bl = 0x32 ;
   int86( 0x10, &regs, &regs ) ;

   // VGA returns 0x12, all others leave it unmodified!
   if ( regs.h.al == 0x12 )
   {
      return videoCard_vga ;
   }

   // EGA/VGA get configuration info
   regs.h.ah = 0x12 ;
   regs.h.bl = 0x10 ;
   int86( 0x10, &regs, &regs ) ;

   // EGA/VGA returns a 0..3 value here
   if ( regs.h.bl & 0x3 != 0 )
   {
      return videoCard_ega ;
   }

   // Get Video Mode
   regs.h.ah = 0x0F;
   int86( 0x10, &regs, &regs );

   // MDA all we need to detect is video mode 7
   if ( regs.h.al == 0x07 )
   {
      return videoCard_mda ;
   }

   // Second to last byte PCjr BIOS info area
   ptr = (char*)_MK_FP( 0xf000, 0xfffe ) ;
   if ( *ptr == 0xFD )
   {
      return videoCard_pcJr ;
   }

   // Tandys will return FF for second to last byte
   if ( *ptr == 0xFF )
   {
      // All Tandys return 0x21 at this address
      ptr = (char*)_MK_FP( 0xf000, 0xc000 ) ;
      if ( *ptr == 0x21 )
      {
         // Get System Environment
         regs.h.ah = 0xC0 ;
         int86( 0x15, &regs, &regs ) ;

         // Early Tandys do not set the carry bit after a "Get System Environment" Call
         if ( regs.w.cflag & INTR_CF )
         {
            return videoCard_tandy1000 ;
         }
         return videoCard_tandySLTL ;
      }
    }

    // We've eliminated all other cards, must be CGA!
    return videoCard_cga ;
}

// Detects whether or not a CPU is an Intel 8088/8086 or not
int cpuIs8088or8086()
{
   int data = 0 ;

    __asm
    {
       pushf                   ; push original FLAGS
       pop     ax              ; get original FLAGS
       mov     cx, ax          ; save original FLAGS
       and     ax, 0fffh       ; clear bits 12-15 in FLAGS
       push    ax              ; save new FLAGS value on stack
       popf                    ; replace current FLAGS value
       pushf                   ; get new FLAGS
       pop     ax              ; store new FLAGS in AX
       and     ax, 0f000h      ; if bits 12-15 are set, then
       mov     data, ax
    }

    if ( data & 0xf000 )
    {
        return 1 ;
    }
    return 0 ;
}

// Return whether we detect a Tandy 8088/8086 or not
int isTandy1000()
{
   return ( detectVideoMode() == videoCard_tandy1000 || detectVideoMode() == videoCard_tandySLTL );
}
