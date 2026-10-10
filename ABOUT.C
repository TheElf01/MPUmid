#include <string.h>

#include "screen.h"
#include "about.h"

#define AB_W   63
#define AB_H   20
#define AB_X   ((SCR_W - AB_W) / 2 + 1)
#define AB_Y   ((SCR_H - (AB_H + 1)) / 2 + 1)
#define TEXT_X (AB_X + 3)
#define TEXT_W (AB_W - 6)

#define G_LEFT      217
#define G_SHADOW_R  217
#define G_SHADOW_B  209
#define A_FRAME     0x03
#define A_SOLID     0x30
#define A_FRAMETXT  0x30
#define A_INNER     0x07
#define A_AUTHOR    0x0F
#define A_ES        0x0B
#define A_EN        0x07

static const char txt_title[]  = "MPUmid - Midi Player";
static const char txt_author[] = "1994-2026  TheElf";
static const char txt_es[] =
  "Gracias por usar mi reproductor de MIDI. La primera versi\xA2n la "
  "termin\x82 en 1994 como encargo de un cliente aficionado a la m\xA3sica "
  "electr\xA2nica. Hab\xA1" "a perdido el c\xA2" "digo en un disco duro que "
  "muri\xA2, pero hace poco logr\x82 recuperarlo: lo termin\x82, pul\xA1 un "
  "poco todo y presento esta nueva versi\xA2n.";
static const char txt_en[] =
  "Thanks for using my MIDI player. I finished the first version in 1994 "
  "as a commission for a client who was into electronic music. I had lost "
  "the source code on a hard drive that died, but I recently managed to "
  "recover it: I finished it, polished everything a bit, and here is this "
  "new version.";
static const char txt_close[]  = "Presione cualquier tecla para cerrar / any key to close";

static unsigned short saved[(AB_H + 1) * (AB_W + 1)];
static int is_open = 0;

static void center(int y, const char *s, unsigned char attr) {
  scr_puts(AB_X + (AB_W - (int)strlen(s)) / 2, y, s, attr);
}

static int wrap(int y, const char *s, unsigned char attr) {
  char line[TEXT_W + 1];
  while (*s) {
    int n = 0, cut = -1;
    while ((s[n] != 0) && (n < TEXT_W)) {
      if (s[n] == ' ') cut = n;
      n++;
    }
    if ((s[n] != 0) && (cut > 0)) n = cut;
    memcpy(line, s, n);
    line[n] = 0;
    scr_puts(TEXT_X, y++, line, attr);
    s += n;
    while (*s == ' ') s++;
  }
  return y;
}

void about_open(void) {
  int x, y, i = 0;
  if (is_open) return;
  for (y = 0; y <= AB_H; y++)
    for (x = 0; x <= AB_W; x++)
      saved[i++] = scr_getcell(AB_X + x, AB_Y + y);
  is_open = 1;

  for (y = 0; y < AB_H; y++) {
    int row = AB_Y + y;
    scr_putc(AB_X, row, G_LEFT, A_FRAME);
    if ((y == 0) || (y == AB_H - 1)) {
      scr_fill(AB_X + 1, row, AB_W - 1, ' ', A_SOLID);
    } else {
      scr_fill(AB_X + 1, row, AB_W - 2, ' ', A_INNER);
      scr_putc(AB_X + AB_W - 1, row, ' ', A_SOLID);
    }
    scr_putc(AB_X + AB_W, row, G_SHADOW_R, A_SOLID);
  }
  scr_fill(AB_X, AB_Y + AB_H, AB_W, G_SHADOW_B, A_SOLID);

  {
    char t[40];
    strcpy(t, " ");
    strcat(t, txt_title);
    strcat(t, " ");
    center(AB_Y, t, A_FRAMETXT);
  }
  center(AB_Y + 2, txt_author, A_AUTHOR);
  y = wrap(AB_Y + 4, txt_es, A_ES);
  y = wrap(y + 1, txt_en, A_EN);
  {
    char buf[64];
    strcpy(buf, " ");
    strcat(buf, txt_close);
    strcat(buf, " ");
    center(AB_Y + AB_H - 1, buf, A_FRAMETXT);
  }
}

void about_close(void) {
  int x, y, i = 0;
  if (!is_open) return;
  for (y = 0; y <= AB_H; y++)
    for (x = 0; x <= AB_W; x++)
      scr_setcell(AB_X + x, AB_Y + y, saved[i++]);
  is_open = 0;
}

int about_is_open(void) {
  return is_open;
}
