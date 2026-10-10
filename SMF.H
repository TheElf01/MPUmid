#ifndef SMF_H
#define SMF_H

#define SMF_MAXTRACKS 64

#define SMF_EV_NOP    0
#define SMF_EV_CHAN   1
#define SMF_EV_TEMPO  2
#define SMF_EV_SYSEX  3

struct smf_event {
  unsigned long delta;
  unsigned char type;
  unsigned char status;
  unsigned char d1, d2;
  unsigned char len;
  unsigned long sysexlen;
  unsigned long tempo;
};

int smf_open(const char *fname, int *format, int *ntracks, unsigned short *timeunitdiv);

int smf_next(struct smf_event *ev);

int smf_sysex_byte(void);

void smf_close(void);

unsigned long ticks_to_us(unsigned long ticks, unsigned long tempo, unsigned short div);

unsigned long smf_duration(const char *fname);

int smf_info(const char *fname, char *name, int namesz, char *copyright, int copysz);

#endif
