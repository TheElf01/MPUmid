#include <string.h>
#include <ctype.h>

#include "screen.h"
#include "effect.h"

#define MAX_BLOCKS  4
#define NPART       40
#define TICK_US     40000UL

#define STAR_SPEED_FAR   6
#define STAR_SPEED_NEAR  16
#define SNOW_SPEED_FAR   3
#define SNOW_SPEED_NEAR  6

#define GLYPH_FAR   '.'
#define GLYPH_NEAR  0x07

#define A_STAR      0x08
#define A_SNOW_FAR  0x08
#define A_SNOW_NEAR 0x07

#define FX          16
#define MAXX        (SCR_W * FX)
#define MAXY        (SCR_H * FX)

struct part {
  int x, y;
  unsigned char is_near;
  signed char sx, sy;
};

static int fx_type = EFFECT_NONE;
static struct part parts[NPART];
static int nblocks = 0;
static int bx[MAX_BLOCKS], by[MAX_BLOCKS], bw[MAX_BLOCKS], bh[MAX_BLOCKS];
static unsigned long next_tick = 0;
static unsigned int seed = 12345;

static unsigned int rnd(unsigned int n) {
  seed = seed * 25173u + 13849u;
  return (seed >> 4) % n;
}

static int block_at(int x, int y) {
  int i;
  for (i = 0; i < nblocks; i++) {
    if ((x >= bx[i]) && (x < bx[i] + bw[i]) && (y >= by[i]) && (y < by[i] + bh[i])) return i;
  }
  return -1;
}

#define COL(p) (1 + (p)->x / FX)
#define ROW(p) (1 + (p)->y / FX)

int effect_from_name(const char *name) {
  if ((stricmp(name, "STARS") == 0) || (stricmp(name, "ESTRELLAS") == 0) ||
      (stricmp(name, "ESPACIO") == 0) || (stricmp(name, "SPACE") == 0) ||
      (strcmp(name, "1") == 0)) return EFFECT_STARS;
  if ((stricmp(name, "SNOW") == 0) || (stricmp(name, "NIEVE") == 0) ||
      (strcmp(name, "2") == 0)) return EFFECT_SNOW;
  return EFFECT_NONE;
}

void effect_block(int x, int y, int w, int h) {
  if (nblocks >= MAX_BLOCKS) return;
  bx[nblocks] = x; by[nblocks] = y; bw[nblocks] = w; bh[nblocks] = h;
  nblocks++;
}

static int free_rows(int col) {
  int y, n = 0;
  for (y = 1; y <= SCR_H; y++) if (block_at(col, y) < 0) n++;
  return n;
}

static void spawn(struct part *p, int anywhere) {
  int tries = 0;
  p->is_near = (unsigned char)(rnd(4) == 0);
  p->sx = -1; p->sy = -1;
  do {
    if (anywhere) {
      p->x = (int)rnd(MAXX);
      p->y = (int)rnd(MAXY);
    } else if (fx_type == EFFECT_STARS) {
      p->x = MAXX - 1 - (int)rnd(FX);
      p->y = (int)rnd(SCR_H) * FX;
    } else {
      int k, best = -1, bestn = -1;
      for (k = 0; k < 3; k++) {
        int c = (int)rnd(SCR_W);
        int n = free_rows(c + 1);
        if (n > bestn) { bestn = n; best = c; }
      }
      p->x = best * FX;
      p->y = (int)rnd(FX);
    }
  } while ((block_at(COL(p), ROW(p)) >= 0) && (++tries < 20));
}

void effect_init(int type) {
  int i;
  fx_type = type;
  if (fx_type == EFFECT_NONE) return;
  for (i = 0; i < NPART; i++) spawn(&parts[i], 1);
  next_tick = 0;
}

static void erase(struct part *p) {
  if ((p->sx > 0) && (block_at(p->sx, p->sy) < 0)) scr_putc(p->sx, p->sy, ' ', 0x07);
  p->sx = -1;
}

void effect_tick(unsigned long now) {
  int i, b, guard;
  if (fx_type == EFFECT_NONE) return;

  {
    long d = (long)(now - next_tick);
    if ((d < 0) && (d >= -(long)(TICK_US * 4))) return;
  }
  next_tick = now + TICK_US;

  for (i = 0; i < NPART; i++) {
    struct part *p = &parts[i];
    int col, row;
    unsigned char glyph, attr;

    if (fx_type == EFFECT_STARS) {
      p->x -= p->is_near ? STAR_SPEED_NEAR : STAR_SPEED_FAR;

      guard = 0;
      while ((p->x >= 0) && ((b = block_at(COL(p), ROW(p))) >= 0) && (guard++ < MAX_BLOCKS))
        p->x -= bw[b] * FX;
      if (p->x < 0) { erase(p); spawn(p, 0); }
      glyph = (unsigned char)(p->is_near ? GLYPH_NEAR : GLYPH_FAR);
      attr = A_STAR;
    } else {
      p->y += p->is_near ? SNOW_SPEED_NEAR : SNOW_SPEED_FAR;

      if (rnd(8) == 0) {
        p->x += rnd(2) ? FX : -FX;
        if (p->x < 0) p->x += MAXX;
        if (p->x >= MAXX) p->x -= MAXX;
      }

      guard = 0;
      while ((p->y < MAXY) && ((b = block_at(COL(p), ROW(p))) >= 0) && (guard++ < MAX_BLOCKS))
        p->y += bh[b] * FX;
      if (p->y >= MAXY) { erase(p); spawn(p, 0); }
      glyph = (unsigned char)(p->is_near ? GLYPH_NEAR : GLYPH_FAR);
      attr = (unsigned char)(p->is_near ? A_SNOW_NEAR : A_SNOW_FAR);
    }

    col = COL(p);
    row = ROW(p);
    if ((col == p->sx) && (row == p->sy)) continue;
    erase(p);
    if (block_at(col, row) < 0) {
      scr_putc(col, row, glyph, attr);
      p->sx = (signed char)col;
      p->sy = (signed char)row;
    }
  }
}
