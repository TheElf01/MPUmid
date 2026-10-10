#include <i86.h>
#include "screen.h"

static unsigned short far *vram = (unsigned short far *)MK_FP(0xB800, 0);

void scr_mode3(void) {
  union REGS r;
  r.x.ax = 0x0003;
  int86(0x10, &r, &r);
}

void scr_hide_cursor(void) {
  union REGS r;
  r.h.ah = 0x01;
  r.x.cx = 0x2000;
  int86(0x10, &r, &r);
}

void scr_noblink(void) {
  union REGS r;
  r.x.ax = 0x1003;
  r.h.bl = 0x00;
  int86(0x10, &r, &r);
}

void scr_cls(unsigned char attr) {
  unsigned short cell = (unsigned short)(((unsigned short)attr << 8) | ' ');
  unsigned short far *p = vram;
  int n;
  for (n = 0; n < SCR_W * SCR_H; n++) *p++ = cell;
}

void scr_putc(int x, int y, unsigned char c, unsigned char attr) {
  x--; y--;
  if ((x < 0) || (x >= SCR_W) || (y < 0) || (y >= SCR_H)) return;
  vram[y * SCR_W + x] = (unsigned short)(((unsigned short)attr << 8) | c);
}

void scr_puts(int x, int y, const char *s, unsigned char attr) {
  while (*s) scr_putc(x++, y, (unsigned char)*s++, attr);
}

void scr_puts_pad(int x, int y, const char *s, int w, unsigned char attr) {
  while ((w > 0) && *s) { scr_putc(x++, y, (unsigned char)*s++, attr); w--; }
  while (w-- > 0) scr_putc(x++, y, ' ', attr);
}

void scr_fill(int x, int y, int w, unsigned char c, unsigned char attr) {
  while (w-- > 0) scr_putc(x++, y, c, attr);
}

unsigned short scr_getcell(int x, int y) {
  x--; y--;
  if ((x < 0) || (x >= SCR_W) || (y < 0) || (y >= SCR_H)) return 0;
  return vram[y * SCR_W + x];
}

void scr_setcell(int x, int y, unsigned short cell) {
  x--; y--;
  if ((x < 0) || (x >= SCR_W) || (y < 0) || (y >= SCR_H)) return;
  vram[y * SCR_W + x] = cell;
}
