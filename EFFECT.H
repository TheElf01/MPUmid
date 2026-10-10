#ifndef EFFECT_H
#define EFFECT_H

#define EFFECT_NONE  0
#define EFFECT_STARS 1
#define EFFECT_SNOW  2

int effect_from_name(const char *name);

void effect_init(int type);

void effect_block(int x, int y, int w, int h);

void effect_tick(unsigned long now);

#endif
