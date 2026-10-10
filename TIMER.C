#include <stdlib.h>
#include <dos.h>
#include <conio.h>
#include "timer.h"

#define PIT_DIVISOR   1024U
#define CHAIN_EVERY   64U
#define TICK_US       858UL
#define TICK_FRAC     2105U
#define FRAC_ONE      10000U

static void (__interrupt __far *bios_tick)(void) = 0;
static volatile unsigned long clock_us = 0;
static volatile unsigned int clock_frac = 0;
static unsigned int chain_count = 0;
static int running = 0;

static void __interrupt __far fast_tick(void) {
  clock_us += TICK_US;
  clock_frac += TICK_FRAC;
  if (clock_frac >= FRAC_ONE) {
    clock_frac -= FRAC_ONE;
    clock_us++;
  }
  if (++chain_count >= CHAIN_EVERY) {
    chain_count = 0;
    _chain_intr(bios_tick);
  }
  outp(0x20, 0x20);
}

static void pit_set(unsigned int divisor) {
  outp(0x43, 0x36);
  outp(0x40, divisor & 0xFF);
  outp(0x40, divisor >> 8);
}

void timer_init(void) {
  if (running) return;
  _disable();
  bios_tick = _dos_getvect(0x08);
  _dos_setvect(0x08, fast_tick);
  clock_us = 0;
  clock_frac = 0;
  chain_count = 0;
  pit_set(PIT_DIVISOR);
  running = 1;
  _enable();
  atexit(timer_stop);
}

void timer_stop(void) {
  if (!running) return;
  _disable();
  pit_set(0);
  _dos_setvect(0x08, bios_tick);
  running = 0;
  _enable();
}

void timer_read(unsigned long *us) {
  _disable();
  *us = clock_us;
  _enable();
}
