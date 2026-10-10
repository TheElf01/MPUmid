#ifndef BROWSER_H
#define BROWSER_H

int browser_init(const char *root);

#define BR_NONE 0
#define BR_PLAY 1
#define BR_ESC  2
#define BR_NEXT 4
#define BR_PREV 5
#define BR_PLAYALL 3

void browser_draw(void);

int browser_key(int k, char *out);

int browser_getkey(void);

int browser_playlist_next(char *out);
void browser_playlist_stop(void);
int browser_playlist_prev(char *out);

int browser_playlist_info(int *pos, int *count);

const char *browser_title(void);

const char *browser_title_folder(void);

int browser_title_is_loose(void);

void browser_set_title(const char *t);

void browser_set_counter(const char *c);

void browser_clean_text(char *s);

void browser_status(const char *msg);

void browser_area(int *x, int *y, int *w, int *h);

void browser_done(void);

#endif
