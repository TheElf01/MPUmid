#ifndef SCREEN_H
#define SCREEN_H

#define SCR_W 80
#define SCR_H 25

void scr_mode3(void);
void scr_hide_cursor(void);
void scr_noblink(void);
void scr_cls(unsigned char attr);
void scr_putc(int x, int y, unsigned char c, unsigned char attr);
void scr_puts(int x, int y, const char *s, unsigned char attr);

void scr_puts_pad(int x, int y, const char *s, int w, unsigned char attr);
void scr_fill(int x, int y, int w, unsigned char c, unsigned char attr);

unsigned short scr_getcell(int x, int y);
void scr_setcell(int x, int y, unsigned short cell);

#endif
