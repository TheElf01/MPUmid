#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <i86.h>

#include "smf.h"
#include "timer.h"
#include "vgafont.h"
#include "screen.h"
#include "browser.h"
#include "effect.h"
#include "about.h"
#include <bios.h>
#include <signal.h>
#include <ctype.h>
#include <dos.h>

#define key_ready() (_bios_keybrd(_KEYBRD_READY) != 0)

#define SEND_SYSEX 1


#define POLL_US      10000UL

#define MAX_LATE_US  300000UL

#define TIME_REACHED(now, t) ((long)((now) - (t)) >= 0)

static int time_due(unsigned long now, unsigned long *target, unsigned long period) {
  long d = (long)(now - *target);
  if ((d >= 0) || (d < -(long)(period * 4))) {
    *target = now + period;
    return 1;
  }
  return 0;
}

static unsigned mpu_port = 0x330;
#define MPU_DATA (mpu_port)
#define MPU_STAT (mpu_port + 1)
#define MPU_CMD  (mpu_port + 1)

static int ui_active = 0;

static int mpu401_init(void);
static void mpu401_reset(void);
static void mpu401_write(unsigned char byte);
static void mpu401_all_notes_off(void);
static void mpu401_send_gm_reset(void);
static void mpu401_drain(void);
static int gui_key(int k, char *out);

static void mpu401_reset(void) {
  unsigned int timeout;

  for (timeout = 0xFFFF; timeout > 0; timeout--) {
    if (!(inp(MPU_STAT) & 0x40)) break;
  }

  outp(MPU_CMD, 0xFF);

  delay(10);
  for (timeout = 1000; timeout > 0; timeout--) {
    if (!(inp(MPU_STAT) & 0x80)) {
      inp(MPU_DATA);
    }
  }
}

static int mpu401_init(void) {
  unsigned int timeout;

  mpu401_reset();

  for (timeout = 0xFFFF; timeout > 0; timeout--) {
    if (!(inp(MPU_STAT) & 0x40)) break;
  }
  if (timeout == 0) return 0;

  outp(MPU_CMD, 0x3F);

  for (timeout = 0xFFFF; timeout > 0; timeout--) {
    if (!(inp(MPU_STAT) & 0x80)) {
      if (inp(MPU_DATA) == 0xFE) return 1;
    }
  }

  return 0;
}

static int mpu_recovered = 0;

static int mpu_dead = 0;

static void mpu401_write(unsigned char byte) {
  unsigned int timeout;
  int attempt;
  if (mpu_dead) return;
  for (attempt = 0; attempt < 2; attempt++) {
    timeout = 0xFFFFu;
    while (inp(MPU_STAT) & 0x40) {
      if (!(inp(MPU_STAT) & 0x80)) (void)inp(MPU_DATA);
      if (--timeout == 0) break;
    }
    if (timeout != 0) {
      outp(MPU_DATA, byte);
      return;
    }
    if (attempt == 0) {
      if (mpu401_init()) {
        mpu_recovered = 1;
      } else {
        mpu_dead = 1;
        mpu_recovered = 1;
        return;
      }
    }
  }
}

static void mpu401_drain(void) {
  int n = 32;
  while ((n-- > 0) && !(inp(MPU_STAT) & 0x80)) (void)inp(MPU_DATA);
}

static void mpu401_all_notes_off(void) {
    int chan;
    for (chan = 0; chan < 16; chan++) {
        mpu401_write((unsigned char)(0xB0 | chan));
        mpu401_write(123);
        mpu401_write(0);

        mpu401_write((unsigned char)(0xB0 | chan));
        mpu401_write(121);
        mpu401_write(0);
    }
}

static void mpu401_send_gm_reset(void) {
    mpu401_write(0xF0);
    mpu401_write(0x7E);
    mpu401_write(0x7F);
    mpu401_write(0x09);
    mpu401_write(0x01);
    mpu401_write(0xF7);

    delay(50);
}

#define BOX_WIDTH  57
#define BOX_HEIGHT 7

#define START_X ((SCR_W - (BOX_WIDTH - 1)) / 2 + 1)
#define START_Y 3

#define BLACK         0
#define CYAN          3
#define LIGHT_GREY    7
#define LIGHT_GREEN   10

#define EQ_ROW_TOP (START_Y + 2)
#define EQ_ROW_BOT (START_Y + 3)
#define EQ_COL   (START_X + 16)
#define EQ_CHANS 16
#define EQ_SEGS  4
#define EQ_MAXLVL (EQ_SEGS * 2)
#define EQ_ATTR  ((BLACK << 4) | LIGHT_GREEN)
#define EQ_DECAY_US 40000UL

static const unsigned char eq_glyph[EQ_SEGS + 1] = { 241, 237, 238, 239, 240 };
static unsigned char eq_level[EQ_CHANS];
static unsigned char eq_shown[EQ_CHANS];

static const unsigned char TILEMAP[BOX_HEIGHT][BOX_WIDTH] = {
    {208,209,209,209,209,209,209,209,209,209,209,209,209,209,209,206,207,209,209,209,209,209,209,209,209,209,209,209,210,211,212,213,214,209,209,215,215,215,215,215,209,209,209,206,207,209,209,209,209,209,209,209,209,209,209,209,217},
    {217,218,219,220,221,224,32,233,234,235,236,32,32,32,32,208,209,209,209,209,209,209,209,209,209,209,209,209,209,209,209,209,209,217,32,32,32,224,32,32,32,32,32,225,226,32,227,228,32,229,230,204,231,232,32,32,217},
    {217,32,32,32,32,32,32,242,243,244,245,32,32,32,32,217,241,241,241,241,241,241,241,241,238,241,241,241,241,241,241,241,32,217,32,32,32,224,32,32,32,32,32,229,230,204,231,232,32,229,230,204,231,232,32,32,217},
    {217,32,32,32,32,32,32,32,32,32,32,32,32,32,32,217,241,237,238,239,241,241,237,238,240,241,241,241,241,241,241,241,32,217,32,32,32,32,32,32,32,32,32,229,230,204,231,232,32,229,230,204,231,232,32,32,217},
    {217,183,184,185,202,32,32,32,222,223,32,32,32,32,32,251,175,175,175,175,175,175,175,175,175,175,175,175,175,175,175,175,252,217,32,246,247,248,249,250,32,32,32,229,230,204,231,232,32,229,230,204,231,232,32,32,217},
    {251,189,190,191,203,252,252,252,187,188,252,252,252,252,252,253,254,255,192,193,194,195,196,197,198,199,205,205,205,205,205,205,205,205,205,178,179,180,181,182,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,217},
    {205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,32}
};

static const unsigned char COLORMAP[BOX_HEIGHT][BOX_WIDTH] = {
    {0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x0F,0x0F,0x0F,0x0F,0x0F,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x30},
    {0x03,0x03,0x03,0x03,0x03,0x32,0x30,0x03,0x03,0x03,0x03,0x30,0x30,0x30,0x30,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x03,0x30,0x07,0x07,0x0F,0x07,0x07,0x30,0x30,0x30,0x03,0x03,0x30,0x03,0x03,0x30,0x03,0x03,0x03,0x03,0x03,0x30,0x30,0x30},
    {0x03,0x30,0x30,0x30,0x30,0x30,0x30,0x03,0x03,0x03,0x03,0x30,0x30,0x30,0x30,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0xA0,0x03,0x30,0x07,0x07,0x0F,0x07,0x07,0x30,0x30,0x30,0x03,0x03,0x03,0x03,0x03,0x30,0x03,0x03,0x03,0x03,0x03,0x30,0x30,0x30},
    {0x03,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0xA0,0x03,0x30,0x07,0x07,0x07,0x07,0x07,0x30,0x30,0x30,0x03,0x03,0x03,0x03,0x03,0x30,0x03,0x03,0x03,0x03,0x03,0x30,0x30,0x30},
    {0x03,0x03,0x03,0x03,0x03,0x30,0x30,0x30,0x03,0x03,0x30,0x30,0x30,0x30,0x30,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x03,0x30,0x0F,0x0F,0x0F,0x0F,0x0F,0x30,0x30,0x30,0x03,0x03,0x03,0x03,0x03,0x30,0x03,0x03,0x03,0x03,0x03,0x30,0x30,0x30},
    {0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x0F,0x0F,0x0F,0x0F,0x0F,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x30},
    {0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x07}
};

static void eq_draw(int c, unsigned char lvl) {
  unsigned char bot = (unsigned char)((lvl > EQ_SEGS) ? EQ_SEGS : lvl);
  unsigned char top = (unsigned char)((lvl > EQ_SEGS) ? lvl - EQ_SEGS : 0);
  scr_putc(EQ_COL + c, EQ_ROW_BOT, eq_glyph[bot], EQ_ATTR);
  scr_putc(EQ_COL + c, EQ_ROW_TOP, eq_glyph[top], EQ_ATTR);
}

static void eq_reset(void) {
  int c;
  if (!ui_active) return;
  for (c = 0; c < EQ_CHANS; c++) {
    eq_level[c] = 0;
    eq_shown[c] = 0;
    eq_draw(c, 0);
  }
}

static void draw_interface(void) {
  int f, c;
  for (f = 0; f < BOX_HEIGHT; f++) {
    for (c = 0; c < BOX_WIDTH; c++) {
      scr_putc(START_X + c, START_Y + f, TILEMAP[f][c], COLORMAP[f][c]);
    }
  }
  eq_reset();
}

static void eq_note(unsigned char chan, unsigned char velocity) {
  unsigned char lvl;
  if (!ui_active) return;
  if (velocity == 0) return;
  lvl = (unsigned char)((velocity + 15) >> 4);
  if (lvl > EQ_MAXLVL) lvl = EQ_MAXLVL;
  chan &= 0x0F;
  if (lvl > eq_level[chan]) eq_level[chan] = lvl;
}

#define TIME_ROW  (START_Y + 1)
#define TIME_L_X  (START_X + 17)
#define TIME_R_X  (START_X + 27)
#define TIME_ATTR ((BLACK << 4) | LIGHT_GREEN)
#define LCD_COLON 23
static const unsigned char lcd_digit[10] = { 8, 9, 10, 11, 12, 15, 19, 20, 21, 22 };
static unsigned long time_total = 0, time_elapsed = 0;
static int time_on = 0;

static void lcd_time_put(int x, unsigned long secs) {
  unsigned long mm = secs / 60UL, ss = secs % 60UL;
  if (mm > 99UL) { mm = 99UL; ss = 59UL; }
  scr_putc(x,     TIME_ROW, lcd_digit[mm / 10], TIME_ATTR);
  scr_putc(x + 1, TIME_ROW, lcd_digit[mm % 10], TIME_ATTR);
  scr_putc(x + 2, TIME_ROW, LCD_COLON,          TIME_ATTR);
  scr_putc(x + 3, TIME_ROW, lcd_digit[ss / 10], TIME_ATTR);
  scr_putc(x + 4, TIME_ROW, lcd_digit[ss % 10], TIME_ATTR);
}

static void lcd_time_show(void) {
  if (!ui_active || !time_on || about_is_open()) return;
  lcd_time_put(TIME_L_X, time_total);
  lcd_time_put(TIME_R_X, time_elapsed);
}

static void lcd_time_clear(void) {
  int i;
  time_on = 0;
  if (!ui_active) return;
  for (i = 0; i < 5; i++) {
    int cl = TIME_L_X - START_X + i, cr = TIME_R_X - START_X + i;
    scr_putc(TIME_L_X + i, TIME_ROW, TILEMAP[1][cl], COLORMAP[1][cl]);
    scr_putc(TIME_R_X + i, TIME_ROW, TILEMAP[1][cr], COLORMAP[1][cr]);
  }
}

static void eq_force_redraw(void) {
  int c;
  for (c = 0; c < EQ_CHANS; c++) eq_shown[c] = 0xFF;
  lcd_time_show();
}

static void eq_tick(unsigned long now) {
  static unsigned long next_decay = 0;
  int c;
  int decay;
  if (!ui_active) return;
  if (about_is_open()) return;
  decay = time_due(now, &next_decay, EQ_DECAY_US);
  for (c = 0; c < EQ_CHANS; c++) {
    if (eq_level[c] != eq_shown[c]) {
      eq_draw(c, eq_level[c]);
      eq_shown[c] = eq_level[c];
    }
    if (decay && eq_level[c]) eq_level[c]--;
  }
}

#define INI_NAME "MPUMID.INI"
#define APP_TITLE "MPUmid - Midi Player"
#define A_APPTITLE 0x0A

static char ini_path[80];
static int ini_effect = EFFECT_NONE;

struct ini_color { int set; unsigned char r, g, b; };
static struct ini_color col_body = { 0, 0, 0, 0 };
static struct ini_color col_lcd  = { 0, 0, 0, 0 };
static struct ini_color col_led  = { 1, 0, 255, 0 };

static const unsigned char ega_rgb[16][3] = {
  {  0,  0,  0}, {  0,  0,170}, {  0,170,  0}, {  0,170,170},
  {170,  0,  0}, {170,  0,170}, {170, 85,  0}, {170,170,170},
  { 85, 85, 85}, { 85, 85,255}, { 85,255, 85}, { 85,255,255},
  {255, 85, 85}, {255, 85,255}, {255,255, 85}, {255,255,255}
};

#define CLAMP255(v) (((v) < 0) ? 0 : (((v) > 255) ? 255 : (v)))

static void ini_parse_color(const char *v, struct ini_color *c) {
  int r, g, b;
  if (sscanf(v, "%d , %d , %d", &r, &g, &b) == 3) {
    r = CLAMP255(r);
    g = CLAMP255(g);
    b = CLAMP255(b);
    c->r = (unsigned char)r; c->g = (unsigned char)g; c->b = (unsigned char)b;
    c->set = 1;
  } else if ((sscanf(v, "%d", &r) == 1) && (r >= 0) && (r <= 15)) {
    c->r = ega_rgb[r][0]; c->g = ega_rgb[r][1]; c->b = ega_rgb[r][2];
    c->set = 1;
  }
}

static void set_attr_rgb(int attr, const struct ini_color *c) {
  union REGS r;
  unsigned char dac;
  if (!c->set) return;
  r.x.ax = 0x1007;
  r.h.bl = (unsigned char)attr;
  int86(0x10, &r, &r);
  dac = r.h.bh;
  r.x.ax = 0x1010;
  r.x.bx = dac;
  r.h.dh = (unsigned char)(c->r >> 2);
  r.h.ch = (unsigned char)(c->g >> 2);
  r.h.cl = (unsigned char)(c->b >> 2);
  int86(0x10, &r, &r);
}

static void ini_trim(char *s) {
  char *e = s + strlen(s);
  while ((e > s) && ((e[-1] == '\n') || (e[-1] == '\r') || (e[-1] == ' ') || (e[-1] == '\t'))) *--e = 0;
}

static int ini_load(const char *argv0) {
  char fname[128], line[160];
  FILE *f;
  char *slash;

  ini_path[0] = 0;

  strncpy(fname, argv0, sizeof(fname) - 16);
  fname[sizeof(fname) - 16] = 0;
  slash = strrchr(fname, '\\');
  if (slash == NULL) slash = strrchr(fname, ':');
  if (slash != NULL) strcpy(slash + 1, INI_NAME); else strcpy(fname, INI_NAME);
  f = fopen(fname, "r");
  if (f == NULL) f = fopen(INI_NAME, "r");
  if (f == NULL) return 0;

  while (fgets(line, sizeof(line), f) != NULL) {
    char *k = line, *v;

    if (((unsigned char)k[0] == 0xEF) && ((unsigned char)k[1] == 0xBB) && ((unsigned char)k[2] == 0xBF)) k += 3;
    while ((*k == ' ') || (*k == '\t')) k++;
    if ((*k == ';') || (*k == '#')) continue;
    v = strchr(k, '=');
    if (v == NULL) continue;
    *v++ = 0;
    ini_trim(k);
    while ((*v == ' ') || (*v == '\t')) v++;
    ini_trim(v);
    if (stricmp(k, "EFFECT") == 0) ini_effect = effect_from_name(v);
    else if (stricmp(k, "MPUPORT") == 0) {
      unsigned long pv = strtoul(v, NULL, 16);
      if ((pv >= 0x200) && (pv <= 0x3F0)) mpu_port = (unsigned)pv;
    }
    else if (stricmp(k, "SC55COLOR") == 0) ini_parse_color(v, &col_body);
    else if (stricmp(k, "LCDCOLOR") == 0) ini_parse_color(v, &col_lcd);
    else if (stricmp(k, "LEDCOLOR") == 0) ini_parse_color(v, &col_led);
    else if (stricmp(k, "PATH") == 0) {
      strncpy(ini_path, v, sizeof(ini_path) - 1);
      ini_path[sizeof(ini_path) - 1] = 0;

      {
        int n = (int)strlen(ini_path);
        if ((n > 3) && (ini_path[n - 1] == '\\')) ini_path[n - 1] = 0;
      }
    }
  }
  fclose(f);
  return 1;
}

static int play_file(const char *fname, int gui, char *next) {
  int miditracks, midiformat, r;
  unsigned short timeunitdiv;
  unsigned long tempo = 500000UL;
  unsigned long nexteventtime;
  struct smf_event ev;
  unsigned long next_poll, next_sec;
  int quit = 0;

  if (ui_active) time_total = smf_duration(fname);

  r = smf_open(fname, &midiformat, &miditracks, &timeunitdiv);
  if (r != 0) return -1;

  if (mpu_dead) {
    mpu_dead = 0;
    if (!mpu401_init()) mpu_dead = 1;
  }

  mpu401_all_notes_off();
  mpu401_send_gm_reset();
  eq_reset();

  timer_read(&nexteventtime);

  time_elapsed = 0;
  time_on = 1;
  lcd_time_show();
  next_sec = nexteventtime + 1000000UL;

  next_poll = nexteventtime;

  while (!quit && smf_next(&ev)) {
    unsigned long t;

    if (ev.delta > 0) nexteventtime += ticks_to_us(ev.delta, tempo, timeunitdiv);

    for (;;) {
      timer_read(&t);
      if (time_due(t, &next_poll, POLL_US)) {
        mpu401_drain();
        if (gui && mpu_recovered) {
          browser_status(mpu_dead ? "la MPU-401 no responde" : "MPU-401 trabada: reiniciada");
          mpu_recovered = 0;
        }
        eq_tick(t);
        if (time_due(t, &next_sec, 1000000UL)) {
          time_elapsed++;
          lcd_time_show();
        }
        if (!about_is_open()) effect_tick(t);
        if (key_ready()) {
          int k = browser_getkey();
          if (gui) {
            int br = gui_key(k, next);
            if (br == BR_ESC)  { quit = 1; break; }
            if (br == BR_PLAY) { quit = 2; break; }
            if (br == BR_PLAYALL) { quit = 3; break; }
            if (br == BR_NEXT) { quit = 4; break; }
            if (br == BR_PREV) { quit = 5; break; }
          } else if (k == 0x1B) {
            quit = 1; break;
          }
          timer_read(&t);
        }
      }
      if (TIME_REACHED(t, nexteventtime)) break;
    }
    if (quit) break;

    if ((long)(t - nexteventtime) > (long)MAX_LATE_US) nexteventtime = t;

    switch (ev.type) {
      case SMF_EV_CHAN:
        mpu401_write(ev.status);
        mpu401_write(ev.d1);
        if (ev.len == 3) mpu401_write(ev.d2);
        if ((ev.status & 0xF0) == 0x90) eq_note((unsigned char)(ev.status & 0x0F), ev.d2);
        break;

      case SMF_EV_TEMPO:
        tempo = ev.tempo;
        break;

      case SMF_EV_SYSEX:
#if SEND_SYSEX

        if (ev.status == 0xF0) mpu401_write(0xF0);
        {
          int b;
          while ((b = smf_sysex_byte()) >= 0) mpu401_write((unsigned char)b);
        }
#endif
        break;

      default:
        break;
    }
  }

  mpu401_all_notes_off();
  smf_close();
  eq_reset();
  lcd_time_clear();
  return quit;
}

static int gui_key(int k, char *out) {
  if (about_is_open()) {
    about_close();
    eq_force_redraw();
    return BR_NONE;
  }
  if ((k == 'i') || (k == 'I')) {
    about_open();
    return BR_NONE;
  }
  return browser_key(k, out);
}

static void screen_setup(void) {
  scr_mode3();
  load_custom_font();
  scr_hide_cursor();
  scr_noblink();
  set_attr_rgb(CYAN, &col_body);
  set_attr_rgb(LIGHT_GREEN, &col_lcd);
  set_attr_rgb(2, &col_led);
  scr_cls((BLACK << 4) | LIGHT_GREY);

  scr_puts((SCR_W - (int)(sizeof(APP_TITLE) - 1)) / 2 + 1, 1, APP_TITLE, A_APPTITLE);
  effect_block((SCR_W - (int)(sizeof(APP_TITLE) - 1)) / 2 + 1, 1, (int)(sizeof(APP_TITLE) - 1), 1);
  draw_interface();
}

static void blaster_port(void) {
  const char *b = getenv("BLASTER");
  if (b == NULL) return;
  while (*b) {
    if (((*b == 'P') || (*b == 'p')) && ((b == getenv("BLASTER")) || (b[-1] == ' '))) {
      unsigned long pv = strtoul(b + 1, NULL, 16);
      if ((pv >= 0x200) && (pv <= 0x3F0)) mpu_port = (unsigned)pv;
      return;
    }
    b++;
  }
}

static void print_song_info(const char *fname) {
  char name[80], copyright[80];
  if (!smf_info(fname, name, sizeof(name), copyright, sizeof(copyright))) return;
  browser_clean_text(name);
  browser_clean_text(copyright);
  if (name[0]) printf("%s\n", name);
  if (copyright[0]) printf("%s\n", copyright);
}

static int __far crit_handler(unsigned deverr, unsigned errcode, unsigned __far *devhdr) {
  (void)deverr; (void)errcode; (void)devhdr;
  return _HARDERR_FAIL;
}

static void exe_dir(const char *argv0, char *out, int outsz) {
  char *sl;
  strncpy(out, argv0, outsz - 1);
  out[outsz - 1] = 0;
  sl = strrchr(out, '\\');
  if (sl == NULL) { out[0] = 0; return; }
  if ((sl - out) == 2) sl[1] = 0;
  else *sl = 0;
}

int main(int argc, char **argv) {
  int gui = (argc < 2);
  int have_ini;

  if (argc > 2) {
    fprintf(stderr, "uso: MPUMID [cancion.mid]\n");
    return(1);
  }

  signal(SIGINT, SIG_IGN);
  _harderr(crit_handler);
#ifdef SIGBREAK
  signal(SIGBREAK, SIG_IGN);
#endif

  blaster_port();
  have_ini = ini_load(argv[0]);

  if (mpu401_init() == 0) {
    fprintf(stderr, "MPU-401 no responde en %Xh\n", mpu_port);
    return(1);
  }
  mpu401_send_gm_reset();
  mpu401_all_notes_off();

  if (!gui) {
    int r;
    print_song_info(argv[1]);
    printf("Reproduciendo...\n");
    timer_init();
    r = play_file(argv[1], 0, NULL);
    mpu401_all_notes_off();
    mpu401_reset();
    timer_stop();
    if (r < 0) {
      fprintf(stderr, "no se pudo leer %s\n", argv[1]);
      return(1);
    }
    return(0);
  }

  {
    static const char help[] = "ESPACIO: directorio   \x1B \x1A: tema   I: info   ESC: salir";
    char name[100], next[100], status[64];
    int path_ok, pending = 0;

    ui_active = 1;
    screen_setup();
    timer_init();
    path_ok = (browser_init(ini_path) == 0);
    if (!path_ok) {
      char ed[80];
      exe_dir(argv[0], ed, sizeof(ed));
      if (ed[0]) browser_init(ed);
    }
    if (!have_ini) browser_status("no encontre " INI_NAME);
    else if (path_ok) browser_status(help);

    browser_draw();

    effect_block(START_X, START_Y, BOX_WIDTH, BOX_HEIGHT);
    {
      int bxx, byy, bww, bhh;
      browser_area(&bxx, &byy, &bww, &bhh);
      effect_block(bxx, byy, bww, bhh);
    }
    effect_init(ini_effect);

    for (;;) {
      int r;
      if (!pending) {
        while (!key_ready()) {
          unsigned long t;
          timer_read(&t);
          if (!about_is_open()) effect_tick(t);
        }
        r = gui_key(browser_getkey(), name);
        if (r == BR_ESC) break;
        if ((r != BR_PLAY) && (r != BR_PLAYALL)) continue;
      }
      pending = 0;

      {
        const char *t = browser_title();
        const char *sep = browser_title_is_loose() ? strstr(t, " - ") : NULL;
        const char *song = t;
        char line[80];
        int pos, cnt;
        if ((sep != NULL) && (sep > t) && sep[3]) {
          int n = (int)(sep - t);
          if (n > (int)sizeof(status) - 1) n = (int)sizeof(status) - 1;
          memcpy(status, t, n);
          status[n] = 0;
          browser_set_title(status);
          song = sep + 3;
        } else {
          browser_set_title(browser_title_folder());
        }

        if (browser_playlist_info(&pos, &cnt)) {
          sprintf(line, "%d/%d", pos, cnt);
          browser_set_counter(line);
        } else {
          browser_set_counter(NULL);
        }
        browser_status(song);
      }
      r = play_file(name, 1, next);
      if ((r == 2) || (r == 3)) {
        strcpy(name, next);
        pending = 1;
        continue;
      }
      if (r == 5) {
        browser_playlist_prev(name);
        pending = 1;
        continue;
      }
      if (r == 1) {
        browser_playlist_stop();
      } else if (browser_playlist_next(name)) {
        pending = 1;
        continue;
      }
      browser_set_title(NULL);
      browser_set_counter(NULL);
      browser_status((r < 0) ? "no se pudo leer ese MIDI" : help);
    }
  }

  mpu401_all_notes_off();
  mpu401_reset();
  timer_stop();
  browser_done();
  restore_default_font();
  return(0);
}
