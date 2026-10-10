#include <conio.h>
#include <dos.h>
#include <string.h>
#include "vgafont.h"

static const unsigned char far vga_font_data[4096] = {
#include "vgafont.inc"
};

static void set_8dot_mode(void)
{
  unsigned char misc;

  outp(0x3C4, 0x01);
  outp(0x3C5, (unsigned char)(inp(0x3C5) | 0x01));

  (void)inp(0x3DA);
  outp(0x3C0, 0x13);
  outp(0x3C0, 0x00);
  outp(0x3C0, 0x20);

  misc = (unsigned char)inp(0x3CC);
  outp(0x3C2, (unsigned char)(misc & 0xF3));
}

void load_custom_font(void)
{
  static unsigned char far font_copy[4096];
  void far *fp;

  _fmemcpy(font_copy, vga_font_data, sizeof(font_copy));

  set_8dot_mode();

  fp = (void far *)font_copy;

#ifdef __WATCOMC__
  _asm {
    push bp
    les  bx, fp
    mov  bp, bx
    mov  ax, 0x1110
    mov  bh, 0x10
    mov  bl, 0x00
    mov  cx, 256
    mov  dx, 0
    int  0x10
    pop  bp
  }
#endif
}

void restore_default_font(void)
{
#ifdef __WATCOMC__
  _asm {
    mov ax, 0x0003
    int 0x10
  }
#endif
}
