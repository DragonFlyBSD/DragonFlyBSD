#ifndef _STDLIB_H_
#define _STDLIB_H_

#include <sys/types.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

void *malloc(size_t);
void free(void *);
void *realloc(void *, size_t);
void *calloc(size_t, size_t);

#endif
