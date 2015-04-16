#ifndef CHARSET_H
#define CHARSET_H

int init_charset(char *name);
char *utf2ascii(char *src, char *dest, int size);
char *ascii2utf(char *src, char *dest, int size);

#endif


