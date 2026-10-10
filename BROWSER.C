#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <conio.h>
#include <dos.h>
#include <direct.h>
#include <bios.h>

#include "screen.h"
#include "browser.h"

#define PANEL_W  56
#define PANEL_X  ((SCR_W - PANEL_W) / 2 + 1)
#define PANEL_Y  10
#define PANEL_H  15
#define LIST_Y   (PANEL_Y + 1)
#define VISIBLE  (PANEL_H - 2)
#define INNER_X  (PANEL_X + 1)
#define INNER_W  (PANEL_W - 2)
#define NAME_X   (INNER_X + 4)
#define NAME_W   (INNER_W - 5)

#define G_LEFT      217
#define G_SHADOW_R  217
#define G_SHADOW_B  209
#define G_SOLID     ' '
#define G_UP        0x18
#define G_DOWN      0x19
#define G_NOTE1     0x0D
#define G_NOTE      0x0E

#define A_FRAME     0x03
#define A_FRAMETXT  0x30
#define A_SOLID     0x30
#define A_DIR       0x0F
#define A_FILE      0x0B
#define A_ICON_DIR  0x0E
#define A_ICON_FILE 0x0F
#define A_SEL       0xA0
#define A_EMPTY     0x08

#define MAX_ENTRIES 1000
#define MAX_SHORT   13
#define MAX_LONG    50
#define MAX_DEPTH   16

typedef struct {
  char name[MAX_SHORT];
  unsigned char is_dir;
} Entry;

static Entry entries[MAX_ENTRIES];
static char far longnames[MAX_ENTRIES][MAX_LONG];
static int scan_capped = 0;
static int scan_doserr = 0;

static void far_to_near(char *dst, const char far *src, int max) {
  int i = 0;
  while ((i < max - 1) && src[i]) { dst[i] = src[i]; i++; }
  dst[i] = 0;
}

static void near_to_far(char far *dst, const char *src, int max) {
  int i = 0;
  while ((i < max - 1) && src[i]) { dst[i] = src[i]; i++; }
  dst[i] = 0;
}

static void far_to_far(char far *dst, const char far *src, int max) {
  int i = 0;
  while ((i < max - 1) && src[i]) { dst[i] = src[i]; i++; }
  dst[i] = 0;
}

static const char *disp_name(int i, char *tmp) {
  if (longnames[i][0]) {
    far_to_near(tmp, longnames[i], MAX_LONG);
    return tmp;
  }
  return entries[i].name;
}
static int n_entries = 0;
static int dir_has_subdirs = 0;
static int need_scan = 1;

static int depth = 0;
static int sel_stack[MAX_DEPTH + 1];
static int top_stack[MAX_DEPTH + 1];
static char title_stack[MAX_DEPTH + 1][MAX_LONG];

static char chosen_title[MAX_LONG];
static char chosen_folder[MAX_LONG];
static int chosen_loose = 0;

static unsigned orig_drive = 0;
static char orig_dir[80];

static int is_midi_name(const char *n) {
  const char *dot = strrchr(n, '.');
  if (dot == NULL) return 0;
  return (stricmp(dot, ".MID") == 0) || (stricmp(dot, ".RMI") == 0) ||
         (stricmp(dot, ".KAR") == 0) || (stricmp(dot, ".SMF") == 0);
}

static int cmp_entry(const void *a, const void *b) {
  const Entry *ea = (const Entry *)a, *eb = (const Entry *)b;
  if (ea->is_dir != eb->is_dir) return eb->is_dir - ea->is_dir;
  return stricmp(ea->name, eb->name);
}

static const unsigned char latin1_to_437[96] = {
  ' ', 0xAD,0x9B,0x9C,'o', 0x9D,'|', 0x15,'"', 'c', 0xA6,0xAE,0xAA,'-', 'R', '-',
  'o', '+', '2', '3', '\'','u', 0x14,'.', ',', '1', 0xA7,'>', 0xAC,0xAB,'3', 0xA8,
  'A', 'A', 'A', 'A', 0x8E,0x8F,0x92,0x80,'E', 0x90,'E', 'E', 'I', 'I', 'I', 'I',
  'D', 0xA5,'O', 'O', 'O', 'O', 0x99,'x', 'O', 'U', 'U', 'U', 0x9A,'Y', 'P', 's',
  0x85,0xA0,0x83,'a', 0x84,0x86,0x91,0x87,0x8A,0x82,0x88,0x89,0x8D,0xA1,0x8C,0x8B,
  'd', 0xA4,0x95,0xA2,0x93,'o', 0x94,'/', 'o', 0x97,0xA3,0x96,0x81,'y', 'p', 0x98
};

static unsigned char cp1252_extra(unsigned char c) {
  switch (c) {
    case 0x82: case 0x91: case 0x92: return '\'';
    case 0x84: case 0x93: case 0x94: return '"';
    case 0x85: return '.';
    case 0x96: case 0x97: return '-';
    case 0x8A: return 'S';  case 0x9A: return 's';
    case 0x8E: return 'Z';  case 0x9E: return 'z';
    case 0x9F: return 'Y';
    default:   return 0;
  }
}

static unsigned char unicode_extra(unsigned long u) {
  if ((u == 0x2018) || (u == 0x2019) || (u == 0x201A) || (u == 0x2032)) return '\'';
  if ((u == 0x201C) || (u == 0x201D) || (u == 0x201E) || (u == 0x2033)) return '"';
  if ((u >= 0x2010) && (u <= 0x2015)) return '-';
  if (u == 0x2026) return '.';
  if (u == 0x2022) return 0x07;
  if (u == 0xFF5E) return '~';
  return 0;
}

static unsigned char latin1_char(unsigned long u) {
  if (u < 0x80) return (unsigned char)u;
  if ((u >= 0xA0) && (u <= 0xFF)) return latin1_to_437[u - 0xA0];
  return unicode_extra(u);
}

static int looks_utf8(const unsigned char *s) {
  int multi = 0;
  while (*s) {
    if (*s < 0x80) { s++; continue; }
    if ((*s >= 0xC2) && (*s <= 0xDF) && ((s[1] & 0xC0) == 0x80)) { s += 2; multi = 1; continue; }
    if ((*s >= 0xE0) && (*s <= 0xEF) && ((s[1] & 0xC0) == 0x80) && ((s[2] & 0xC0) == 0x80)) { s += 3; multi = 1; continue; }
    if ((*s >= 0xF0) && (*s <= 0xF4) && ((s[1] & 0xC0) == 0x80) && ((s[2] & 0xC0) == 0x80) && ((s[3] & 0xC0) == 0x80)) { s += 4; multi = 1; continue; }
    return 0;
  }
  return multi;
}

static void clean_name(char *str) {
  unsigned char *s = (unsigned char *)str, *d = (unsigned char *)str;
  int utf8 = looks_utf8(s);
  int latin = 0;
  if (!utf8) {
    const unsigned char *p;
    for (p = s; *p; p++) if (*p >= 0xC0) { latin = 1; break; }
  }
  while (*s) {
    unsigned char out;
    if (*s < 0x80) {
      out = *s++;
    } else if (utf8) {
      unsigned long u;
      if (*s < 0xE0)      { u = ((unsigned long)(s[0] & 0x1F) << 6) | (s[1] & 0x3F); s += 2; }
      else if (*s < 0xF0) { u = ((unsigned long)(s[0] & 0x0F) << 12) | ((unsigned long)(s[1] & 0x3F) << 6) | (s[2] & 0x3F); s += 3; }
      else                { u = 0x10000UL; s += 4; }
      out = latin1_char(u);
    } else if (latin) {
      out = (*s >= 0xA0) ? latin1_to_437[*s - 0xA0] : cp1252_extra(*s);
      s++;
    } else {
      out = *s++;
    }
    if (out) *d++ = out;
  }
  *d = 0;

  {
    unsigned char *r = (unsigned char *)str, *w = (unsigned char *)str;
    while (*r == ' ') r++;
    while (*r) {
      if ((*r == ' ') && ((r[1] == ' ') || (r[1] == 0))) { r++; continue; }
      *w++ = *r++;
    }
    *w = 0;
  }
}

void browser_clean_text(char *s) {
  clean_name(s);
}

static int key_matches(const char *key, const char *name) {
  const char *dot;
  size_t n;
  if (stricmp(key, name) == 0) return 1;
  if (strchr(key, '.') != NULL) return 0;
  dot = strchr(name, '.');
  if (dot == NULL) return 0;
  n = (size_t)(dot - name);
  return (strlen(key) == n) && (strnicmp(key, name, n) == 0);
}

static int parse_lst_line(char *line, char **kp, char **vp) {
  char *k = line, *v, *e;
  while ((*k == ' ') || (*k == '\t')) k++;
  if ((*k == 0) || (*k == ';') || (*k == '#') || (*k == '\n') || (*k == '\r')) return 0;
  v = strchr(k, '=');
  if (v == NULL) {
    v = k;
    while (*v && (*v != ' ') && (*v != '\t')) v++;
    if (*v == 0) return 0;
  }
  *v++ = 0;
  e = k + strlen(k);
  while ((e > k) && ((e[-1] == ' ') || (e[-1] == '\t'))) *--e = 0;
  while ((*v == ' ') || (*v == '\t')) v++;
  e = v + strlen(v);
  while ((e > v) && ((e[-1] == '\n') || (e[-1] == '\r') || (e[-1] == ' ') || (e[-1] == '\t'))) *--e = 0;
  if ((*k == 0) || (*v == 0)) return 0;
  *kp = k;
  *vp = v;
  return 1;
}

static void load_longnames(const char *fname, int want_dir) {
  FILE *f = fopen(fname, "r");
  char line[256];
  char *k, *v;
  int i;
  if (f == NULL) return;
  while (fgets(line, sizeof(line), f) != NULL) {
    if (!parse_lst_line(line, &k, &v)) continue;
    for (i = 0; i < n_entries; i++) {
      if (entries[i].is_dir != (unsigned char)want_dir) continue;
      if (toupper((unsigned char)entries[i].name[0]) != toupper((unsigned char)k[0])) continue;
      if (key_matches(k, entries[i].name)) {
        clean_name(v);
        near_to_far(longnames[i], v, MAX_LONG);
        break;
      }
    }
  }
  fclose(f);
}

static void scan_dir(void) {
  struct find_t ff;
  unsigned err;
  int i;
  n_entries = 0;
  dir_has_subdirs = 0;
  scan_capped = 0;
  scan_doserr = 0;
  err = _dos_findfirst("*.*", _A_NORMAL | _A_RDONLY | _A_ARCH | _A_SUBDIR, &ff);
  if (err == 0) {
    do {
      Entry *e;
      int dir = (ff.attrib & _A_SUBDIR) != 0;
      if (ff.name[0] == '.') continue;
      if (!dir && !is_midi_name(ff.name)) continue;
      if (n_entries >= MAX_ENTRIES) { scan_capped = 1; break; }
      e = &entries[n_entries++];
      strncpy(e->name, ff.name, MAX_SHORT - 1);
      e->name[MAX_SHORT - 1] = 0;
      e->is_dir = (unsigned char)dir;
      if (dir) dir_has_subdirs = 1;
    } while ((err = _dos_findnext(&ff)) == 0);
  }

  if ((err != 0) && (err != 18) && (err != 2) && (err != 3) && !scan_capped) scan_doserr = (int)err;
  qsort(entries, n_entries, sizeof(Entry), cmp_entry);
  for (i = 0; i < n_entries; i++) longnames[i][0] = 0;
  load_longnames("DIR.LST", 1);
  load_longnames("FILES.LST", 0);
  need_scan = 0;
}

static char title_override[MAX_LONG];
static char title_counter[16];

static void draw_title(void) {
  char buf[MAX_LONG + 4];
  int x, room, clen = (int)strlen(title_counter);
  scr_putc(PANEL_X, PANEL_Y, G_LEFT, A_FRAME);
  scr_fill(PANEL_X + 1, PANEL_Y, PANEL_W - 1, G_SOLID, A_SOLID);

  room = PANEL_W - 6 - (clen ? (clen + 3) * 2 : 0);
  sprintf(buf, " %.*s ", room, title_override[0] ? title_override : title_stack[depth]);
  x = PANEL_X + (PANEL_W - (int)strlen(buf)) / 2;
  scr_puts(x, PANEL_Y, buf, A_FRAMETXT);
  if (clen) scr_puts(PANEL_X + PANEL_W - 2 - clen, PANEL_Y, title_counter, A_FRAMETXT);
}

void browser_set_counter(const char *c) {
  if (c == NULL) title_counter[0] = 0;
  else {
    strncpy(title_counter, c, sizeof(title_counter) - 1);
    title_counter[sizeof(title_counter) - 1] = 0;
  }
  draw_title();
}

static char status_text[PANEL_W];

static void draw_status(void) {
  char buf[PANEL_W + 4];
  scr_putc(PANEL_X, PANEL_Y + PANEL_H - 1, G_LEFT, A_FRAME);
  scr_fill(PANEL_X + 1, PANEL_Y + PANEL_H - 1, PANEL_W - 1, G_SOLID, A_SOLID);
  if (status_text[0]) {
    sprintf(buf, " %.*s ", PANEL_W - 6, status_text);
    scr_puts(PANEL_X + (PANEL_W - (int)strlen(buf)) / 2,
             PANEL_Y + PANEL_H - 1, buf, A_FRAMETXT);
  }
}

void browser_set_title(const char *t) {
  if (t == NULL) title_override[0] = 0;
  else {
    strncpy(title_override, t, MAX_LONG - 1);
    title_override[MAX_LONG - 1] = 0;
  }
  draw_title();
}

void browser_status(const char *msg) {
  strncpy(status_text, (msg != NULL) ? msg : "", PANEL_W - 1);
  status_text[PANEL_W - 1] = 0;
  draw_status();
}

static void draw_frame(void) {
  int i;

  for (i = 0; i < PANEL_H; i++) scr_putc(PANEL_X + PANEL_W, PANEL_Y + i, G_SHADOW_R, A_SOLID);
  scr_fill(PANEL_X, PANEL_Y + PANEL_H, PANEL_W, G_SHADOW_B, A_SOLID);
  draw_title();
  for (i = 0; i < VISIBLE; i++) {
    scr_putc(PANEL_X, LIST_Y + i, G_LEFT, A_FRAME);
    scr_putc(PANEL_X + PANEL_W - 1, LIST_Y + i, G_SOLID, A_SOLID);
  }
  draw_status();
}

static void draw_row(int pos, int row, int selected) {
  char tmp[MAX_LONG];
  int has_dotdot = (depth > 0);
  int y = LIST_Y + row;
  const char *name;
  int dir;
  unsigned char a_icon, a_name;

  if (pos < 0) {
    scr_fill(INNER_X, y, INNER_W, ' ', A_FILE);
    return;
  }
  if (has_dotdot && (pos == 0)) {
    name = ".."; dir = 1;
  } else {
    name = disp_name(pos - has_dotdot, tmp);
    dir = entries[pos - has_dotdot].is_dir;
  }
  if (selected) { a_icon = A_SEL; a_name = A_SEL; }
  else if (dir) { a_icon = A_ICON_DIR; a_name = A_DIR; }
  else          { a_icon = A_ICON_FILE; a_name = A_FILE; }

  scr_putc(INNER_X, y, ' ', a_name);
  if (dir) {
    scr_putc(INNER_X + 1, y, 0x01, a_icon);
    scr_putc(INNER_X + 2, y, 0x02, a_icon);
  } else {
    scr_putc(INNER_X + 1, y, G_NOTE1, a_icon);
    scr_putc(INNER_X + 2, y, G_NOTE, a_icon);
  }
  scr_putc(INNER_X + 3, y, ' ', a_name);
  scr_puts_pad(NAME_X, y, name, NAME_W + 1, a_name);
}

static void draw_list(int top, int sel, int total) {
  int i;
  for (i = 0; i < VISIBLE; i++) {
    int pos = top + i;
    draw_row((pos < total) ? pos : -1, i, pos == sel);
  }
  if (total == 0) scr_puts(NAME_X, LIST_Y, "(sin archivos MIDI)", A_EMPTY);
  if (scan_capped || scan_doserr) {
    char w[48];
    if (scan_capped) sprintf(w, " solo los primeros %d archivos ", MAX_ENTRIES);
    else sprintf(w, " error DOS %d al leer el directorio ", scan_doserr);
    scr_puts(PANEL_X + PANEL_W - 2 - (int)strlen(w), PANEL_Y + PANEL_H - 1, w, A_FRAMETXT);
  }

  scr_putc(PANEL_X + PANEL_W - 1, LIST_Y, (unsigned char)(top > 0 ? G_UP : G_SOLID),
           (unsigned char)(top > 0 ? A_FRAMETXT : A_SOLID));
  scr_putc(PANEL_X + PANEL_W - 1, LIST_Y + VISIBLE - 1,
           (unsigned char)(top + VISIBLE < total ? G_DOWN : G_SOLID),
           (unsigned char)(top + VISIBLE < total ? A_FRAMETXT : A_SOLID));
}

int browser_init(const char *root) {
  unsigned ndrives;
  int ok = 0;
  _dos_getdrive(&orig_drive);
  if (getcwd(orig_dir, sizeof(orig_dir)) == NULL) orig_dir[0] = 0;
  if ((root != NULL) && *root) {
    if (root[1] == ':') {
      unsigned d, got;
      d = (unsigned)(toupper((unsigned char)root[0]) - 'A' + 1);
      _dos_setdrive(d, &ndrives);
      _dos_getdrive(&got);
      if (got != d) {
        sprintf(status_text, "no existe la unidad %c:", toupper((unsigned char)root[0]));
        ok = -1;
      }
    }

    if ((ok == 0) && !((root[1] == ':') && (root[2] == 0))) {
      if ((chdir(root) != 0) && ((root[1] != ':') || (chdir(root + 2) != 0))) {
        sprintf(status_text, "no existe: %.28s", root);
        ok = -1;
      }
    }
    if (ok != 0) _dos_setdrive(orig_drive, &ndrives);
  }
  depth = 0;
  memset(sel_stack, 0, sizeof(sel_stack));
  memset(top_stack, 0, sizeof(top_stack));
  if ((root != NULL) && *root && (ok == 0)) {
    strncpy(title_stack[0], root, MAX_LONG - 1);
    title_stack[0][MAX_LONG - 1] = 0;
  } else {
    strcpy(title_stack[0], "MIDI");
  }
  need_scan = 1;
  return ok;
}

void browser_done(void) {
  unsigned ndrives;
  _dos_setdrive(orig_drive, &ndrives);
  if (orig_dir[0]) chdir(orig_dir);
}

const char *browser_title(void) {
  return chosen_title;
}

void browser_area(int *x, int *y, int *w, int *h) {
  *x = PANEL_X; *y = PANEL_Y;
  *w = PANEL_W + 1; *h = PANEL_H + 1;
}

const char *browser_title_folder(void) {
  return chosen_folder;
}

int browser_title_is_loose(void) {
  return chosen_loose;
}

static int sel = 0, top = 0, total = 0, has_dotdot = 0;

static void enter_dir_state(void) {
  if (need_scan) scan_dir();
  has_dotdot = (depth > 0);
  total = n_entries + has_dotdot;
  sel = sel_stack[depth];
  top = top_stack[depth];
  if (sel >= total) sel = total - 1;
  if (sel < 0) sel = 0;
}

static void fix_scroll(void) {
  if (top > sel) top = sel;
  if (sel >= top + VISIBLE) top = sel - VISIBLE + 1;
  if (top < 0) top = 0;
  sel_stack[depth] = sel;
  top_stack[depth] = top;
}

void browser_draw(void) {
  enter_dir_state();
  fix_scroll();
  draw_frame();
  draw_list(top, sel, total);
}

static void go_up(void) {
  chdir("..");
  depth--;
  need_scan = 1;
  browser_draw();
}

static char far pl_short[MAX_ENTRIES][MAX_SHORT];
static char far pl_long[MAX_ENTRIES][MAX_LONG];
static char pl_dir[80];
static char pl_folder[MAX_LONG];
static int  pl_loose = 0;
static int  pl_count = 0;
static int  pl_pos = 0;
static int  pl_active = 0;

static void make_path(char *out, const char *dir, const char *name) {
  int n;
  strcpy(out, dir);
  n = (int)strlen(out);
  if ((n > 0) && (out[n - 1] != '\\')) strcat(out, "\\");
  strcat(out, name);
}

static void pl_load(char *out) {
  char nm[MAX_SHORT];
  far_to_near(nm, pl_short[pl_pos], MAX_SHORT);
  make_path(out, pl_dir, nm);
  far_to_near(chosen_title, pl_long[pl_pos], MAX_LONG);
  if (chosen_title[0] == 0) strcpy(chosen_title, nm);
  strcpy(chosen_folder, pl_folder);
  chosen_loose = pl_loose;
}

int browser_playlist_next(char *out) {
  if (!pl_active) return 0;
  if (pl_pos + 1 >= pl_count) { pl_active = 0; return 0; }
  pl_pos++;
  pl_load(out);
  return 1;
}

int browser_playlist_prev(char *out) {
  if (!pl_active) return 0;
  if (pl_pos > 0) pl_pos--;
  pl_load(out);
  return 1;
}

void browser_playlist_stop(void) {
  pl_active = 0;
}

int browser_playlist_info(int *pos, int *count) {
  *pos = pl_pos + 1;
  *count = pl_count;
  return pl_active;
}

static int pl_build(char *out) {
  int i, start = 0;
  int cur = sel - has_dotdot;
  if (getcwd(pl_dir, sizeof(pl_dir)) == NULL) return 0;
  pl_count = 0;
  for (i = 0; i < n_entries; i++) {
    if (entries[i].is_dir) continue;
    if (i == cur) start = pl_count;
    near_to_far(pl_short[pl_count], entries[i].name, MAX_SHORT);
    far_to_far(pl_long[pl_count], longnames[i], MAX_LONG);
    pl_count++;
  }
  if (pl_count == 0) return 0;
  strcpy(pl_folder, title_stack[depth]);
  pl_loose = (depth == 0) || dir_has_subdirs;
  pl_pos = start;
  pl_active = 1;
  pl_load(out);
  return 1;
}

int browser_key(int k, char *out) {
  int old_sel = sel, old_top = top;

  if (k == -72)      { if (sel > 0) sel--; }
  else if (k == -80) { if (sel < total - 1) sel++; }
  else if (k == -73) { sel -= VISIBLE; if (sel < 0) sel = 0; }
  else if (k == -81) { sel += VISIBLE; if (sel > total - 1) sel = total - 1; if (sel < 0) sel = 0; }
  else if (k == -71) { sel = 0; }
  else if (k == -79) { sel = (total > 0) ? total - 1 : 0; }
  else if (k == 27)  { return BR_ESC; }
  else if ((k == -77) && pl_active) { return BR_NEXT; }
  else if ((k == -75) && pl_active) { return BR_PREV; }
  else if (k == ' ')  { return pl_build(out) ? BR_PLAYALL : BR_NONE; }
  else if ((k == 8) && has_dotdot) { go_up(); return BR_NONE; }
  else if ((k == 13) && (total > 0)) {
    if (has_dotdot && (sel == 0)) { go_up(); return BR_NONE; }
    {
      int ei = sel - has_dotdot;
      Entry *e = &entries[ei];
      char tmp[MAX_LONG];
      if (e->is_dir) {
        if ((depth < MAX_DEPTH) && (chdir(e->name) == 0)) {
          depth++;
          sel_stack[depth] = 0;
          top_stack[depth] = 0;
          strcpy(title_stack[depth], disp_name(ei, tmp));
          need_scan = 1;
          browser_draw();
        }
        return BR_NONE;
      }
      {
        char cwd[80];
        if (getcwd(cwd, sizeof(cwd)) == NULL) cwd[0] = 0;
        make_path(out, cwd, e->name);
      }
      pl_active = 0;
      strcpy(chosen_title, disp_name(ei, tmp));
      strcpy(chosen_folder, title_stack[depth]);
      chosen_loose = (depth == 0) || dir_has_subdirs;
      return BR_PLAY;
    }
  }

  if (total > 0) {
    fix_scroll();
    if (top != old_top) {
      draw_list(top, sel, total);
    } else if (sel != old_sel) {
      draw_row(old_sel, old_sel - top, 0);
      draw_row(sel, sel - top, 1);
    }
  }
  return BR_NONE;
}

int browser_getkey(void) {
  unsigned k = _bios_keybrd(_KEYBRD_READ);
  unsigned char ascii = (unsigned char)(k & 0xFF);
  if ((ascii == 0) || (ascii == 0xE0)) return -(int)(k >> 8);
  return ascii;
}
