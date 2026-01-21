#ifndef _STRINGS_H_
#define _STRINGS_H_

#include <sys/types.h>

int bcmp(const void *b1, const void *b2, size_t len);
void bcopy(const void *src, void *dst, size_t len);
void bzero(void *b, size_t len);
int ffs(int i);
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);
char *index(const char *s, int c);
char *rindex(const char *s, int c);

#endif
