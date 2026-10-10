#include <dos.h>
#include <fcntl.h>
#include <io.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "smf.h"

#define SMF_BUFMAX 4096
#define SMF_BUFMIN 256

struct smf_track {
  unsigned char far *buf;
  unsigned short bufsz;
  unsigned short buflen;
  unsigned short bufpos;
  unsigned long filepos;
  unsigned long remain;
  unsigned long nexttick;
  unsigned char status;
  unsigned char done;
};

static struct smf_track trk[SMF_MAXTRACKS];
static int ntrk = 0;
static int fh = -1;
static unsigned long fpos_now = 0xFFFFFFFFUL;
static unsigned long curtick = 0;
static struct smf_track *cur = NULL;
static unsigned long sysex_left = 0;

static int f_readat(unsigned long pos, void far *dst, unsigned short n) {
  unsigned got = 0;
  if (pos != fpos_now) {
    if (lseek(fh, (long)pos, SEEK_SET) < 0) { fpos_now = 0xFFFFFFFFUL; return 0; }
  }
  if (_dos_read(fh, dst, n, &got) != 0) got = 0;
  fpos_now = pos + got;
  return (int)got;
}

static int refill(struct smf_track *t) {
  unsigned short n;
  int got;
  if (t->remain == 0) return -1;
  n = (t->remain < (unsigned long)t->bufsz) ? (unsigned short)t->remain : t->bufsz;
  got = f_readat(t->filepos, t->buf, n);
  if (got <= 0) { t->remain = 0; return -1; }
  t->buflen = (unsigned short)got;
  t->bufpos = 0;
  t->filepos += (unsigned long)got;
  t->remain -= (unsigned long)got;
  return 0;
}

static int getb(struct smf_track *t) {
  if (t->bufpos >= t->buflen) {
    if (refill(t) != 0) { t->done = 1; return -1; }
  }
  return t->buf[t->bufpos++];
}

static void skip(struct smf_track *t, unsigned long n) {
  unsigned short avail = (unsigned short)(t->buflen - t->bufpos);
  if (n <= avail) { t->bufpos = (unsigned short)(t->bufpos + n); return; }
  n -= avail;
  t->bufpos = t->buflen;

  if (n >= t->remain) { t->filepos += t->remain; t->remain = 0; return; }
  t->filepos += n;
  t->remain -= n;
}

static unsigned long vlq(struct smf_track *t) {
  unsigned long v = 0;
  int i, b;
  for (i = 0; i < 4; i++) {
    b = getb(t);
    if (b < 0) return 0;
    v = (v << 7) | (unsigned long)(b & 0x7F);
    if ((b & 0x80) == 0) break;
  }
  return v;
}

static int is_done(struct smf_track *t) {
  if (t->done) return 1;
  if ((t->bufpos >= t->buflen) && (t->remain == 0)) { t->done = 1; return 1; }
  return 0;
}

static unsigned long be32(const unsigned char *p) {
  return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3];
}

int smf_open(const char *fname, int *format, int *ntracks, unsigned short *timeunitdiv) {
  unsigned char h[22];
  unsigned long pos, len;
  unsigned short declared;
  int i;

  ntrk = 0; cur = NULL; sysex_left = 0; curtick = 0; fpos_now = 0xFFFFFFFFUL;
  if (_dos_open(fname, O_RDONLY, &fh) != 0) { fh = -1; return -1; }

  pos = 0;
  if (f_readat(0, h, 14) != 14) goto bad;
  if ((memcmp(h, "RIFF", 4) == 0) && (memcmp(h + 8, "RMID", 4) == 0)) {
    pos = 20;
    if (f_readat(pos, h, 14) != 14) goto bad;
  }
  if ((memcmp(h, "MThd", 4) != 0) || (be32(h + 4) < 6)) goto bad;
  *format = (h[8] << 8) | h[9];
  declared = (unsigned short)((h[10] << 8) | h[11]);
  *timeunitdiv = (unsigned short)((h[12] << 8) | h[13]);
  if (*timeunitdiv & 0x8000u) { smf_close(); return -4; }
  if (*timeunitdiv == 0) goto bad;
  pos += 8 + be32(h + 4);

  while ((ntrk < SMF_MAXTRACKS) && (ntrk < (int)declared)) {
    if (f_readat(pos, h, 8) != 8) break;
    len = be32(h + 4);
    if (memcmp(h, "MTrk", 4) == 0) {
      struct smf_track *t = &trk[ntrk];
      unsigned short sz = (len < SMF_BUFMAX) ? (unsigned short)len : SMF_BUFMAX;
      if (sz == 0) sz = 1;
      memset(t, 0, sizeof(*t));
      t->buf = (unsigned char far *)_fmalloc(sz);
      if ((t->buf == NULL) && (sz > SMF_BUFMIN)) {
        sz = SMF_BUFMIN;
        t->buf = (unsigned char far *)_fmalloc(sz);
      }
      if (t->buf == NULL) { smf_close(); return -3; }
      t->bufsz = sz;
      t->filepos = pos + 8;
      t->remain = len;
      ntrk++;
    }
    pos += 8 + len;
  }
  if (ntrk == 0) goto bad;

  for (i = 0; i < ntrk; i++) {
    trk[i].nexttick = vlq(&trk[i]);
    is_done(&trk[i]);
  }
  *ntracks = ntrk;
  return 0;

bad:
  smf_close();
  return -2;
}

void smf_close(void) {
  int i;
  for (i = 0; i < ntrk; i++) {
    if (trk[i].buf != NULL) _ffree(trk[i].buf);
    trk[i].buf = NULL;
  }
  ntrk = 0;
  cur = NULL;
  if (fh >= 0) _dos_close(fh);
  fh = -1;
}

int smf_sysex_byte(void) {
  int b;
  if ((cur == NULL) || (sysex_left == 0)) return -1;
  b = getb(cur);
  if (b < 0) { sysex_left = 0; return -1; }
  sysex_left--;
  return b;
}

int smf_next(struct smf_event *ev) {
  struct smf_track *t;
  int i, b, st;

  if (cur != NULL) {
    if (sysex_left) { skip(cur, sysex_left); sysex_left = 0; }
    if (!is_done(cur)) {
      cur->nexttick += vlq(cur);
      is_done(cur);
    }
    cur = NULL;
  }

  t = NULL;
  for (i = 0; i < ntrk; i++) {
    if (trk[i].done) continue;
    if ((t == NULL) || (trk[i].nexttick < t->nexttick)) t = &trk[i];
  }
  if (t == NULL) return 0;

  ev->delta = t->nexttick - curtick;
  curtick = t->nexttick;
  ev->type = SMF_EV_NOP;
  cur = t;

  b = getb(t);
  if (b < 0) return 1;

  if (b < 0x80) {
    st = t->status;
    if (st == 0) { t->done = 1; return 1; }
  } else {
    st = b;
    if (st < 0xF0) t->status = (unsigned char)st;
    b = -1;
  }

  if (st < 0xF0) {
    int hi = st & 0xF0;
    if (b < 0) b = getb(t);
    ev->type = SMF_EV_CHAN;
    ev->status = (unsigned char)st;
    ev->d1 = (unsigned char)(b & 0x7F);
    if ((hi == 0xC0) || (hi == 0xD0)) {
      ev->len = 2;
    } else {
      ev->d2 = (unsigned char)(getb(t) & 0x7F);
      ev->len = 3;
    }
  } else if (st == 0xFF) {
    int mt = getb(t);
    unsigned long len = vlq(t);
    if (mt == 0x2F) {
      t->done = 1;
    } else if ((mt == 0x51) && (len == 3)) {
      unsigned long v = (unsigned long)getb(t) << 16;
      v |= (unsigned long)getb(t) << 8;
      v |= (unsigned long)getb(t);
      ev->type = SMF_EV_TEMPO;
      ev->tempo = v;
    } else {
      skip(t, len);
    }
  } else if ((st == 0xF0) || (st == 0xF7)) {
    ev->type = SMF_EV_SYSEX;
    ev->status = (unsigned char)st;
    ev->sysexlen = vlq(t);
    sysex_left = ev->sysexlen;
  } else {
    t->done = 1;
  }
  return 1;
}

int smf_info(const char *fname, char *name, int namesz, char *copyright, int copysz) {
  unsigned char h[22];
  unsigned char *buf;
  unsigned long pos = 0, len;
  unsigned short n, i;
  int fd;
  unsigned got;

  name[0] = 0;
  copyright[0] = 0;
  if (_dos_open(fname, O_RDONLY, &fd) != 0) return 0;

  if ((_dos_read(fd, h, 14, &got) != 0) || (got != 14)) goto done;
  if ((memcmp(h, "RIFF", 4) == 0) && (memcmp(h + 8, "RMID", 4) == 0)) {
    pos = 20;
    lseek(fd, (long)pos, SEEK_SET);
    if ((_dos_read(fd, h, 14, &got) != 0) || (got != 14)) goto done;
  }
  if (memcmp(h, "MThd", 4) != 0) goto done;
  pos += 8 + be32(h + 4);
  for (;;) {
    lseek(fd, (long)pos, SEEK_SET);
    if ((_dos_read(fd, h, 8, &got) != 0) || (got != 8)) goto done;
    len = be32(h + 4);
    if (memcmp(h, "MTrk", 4) == 0) break;
    pos += 8 + len;
  }

  n = (len < 2048UL) ? (unsigned short)len : 2048;
  buf = (unsigned char *)malloc(n);
  if (buf == NULL) goto done;
  if ((_dos_read(fd, buf, n, &got) != 0)) got = 0;
  n = (unsigned short)got;

  i = 0;
  while (i < n) {
    unsigned long mlen = 0;
    unsigned char st;
    int k;

    for (k = 0; (k < 4) && (i < n); k++) { if (!(buf[i++] & 0x80)) break; }
    if (i >= n) break;
    st = buf[i];
    if (st != 0xFF) break;
    i++;
    if (i + 1 >= n) break;
    {
      unsigned char type = buf[i++];
      for (k = 0; (k < 4) && (i < n); k++) {
        mlen = (mlen << 7) | (buf[i] & 0x7F);
        if (!(buf[i++] & 0x80)) break;
      }
      if (i + mlen > n) break;
      if (((type == 0x03) && (name[0] == 0)) || ((type == 0x02) && (copyright[0] == 0))) {
        char *dst = (type == 0x03) ? name : copyright;
        int max = ((type == 0x03) ? namesz : copysz) - 1;
        int m = (mlen < (unsigned long)max) ? (int)mlen : max;
        memcpy(dst, buf + i, m);
        dst[m] = 0;
      }
      if (type == 0x2F) break;
      i = (unsigned short)(i + mlen);
    }
  }
  free(buf);

done:
  _dos_close(fd);
  return (name[0] != 0) || (copyright[0] != 0);
}

static unsigned long mul_div(unsigned long a, unsigned long b, unsigned long c) {
  unsigned long hi = a * (b >> 16);
  unsigned long lo = a * (b & 0xFFFFUL);
  return ((hi / c) << 16) + ((((hi % c) << 16) + lo) / c);
}

unsigned long ticks_to_us(unsigned long ticks, unsigned long tempo, unsigned short div) {
  unsigned long d = div;
  return (ticks / d) * tempo + mul_div(ticks % d, tempo, d);
}


unsigned long smf_duration(const char *fname) {
  int format, ntracks;
  unsigned short div;
  unsigned long sec = 0, rem = 0, tempo = 500000UL;
  struct smf_event ev;

  if (smf_open(fname, &format, &ntracks, &div) != 0) return 0;
  while (smf_next(&ev)) {
    unsigned long d = ev.delta;
    while (d > 0) {
      unsigned long chunk = (d > (unsigned long)div * 100UL) ? (unsigned long)div * 100UL : d;
      unsigned long us = ticks_to_us(chunk, tempo, div);
      sec += us / 1000000UL;
      rem += us % 1000000UL;
      if (rem >= 1000000UL) { sec++; rem -= 1000000UL; }
      d -= chunk;
    }
    if (ev.type == SMF_EV_TEMPO) tempo = ev.tempo;
  }
  smf_close();
  return sec + ((rem >= 500000UL) ? 1 : 0);
}
