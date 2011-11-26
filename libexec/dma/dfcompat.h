#ifndef DFCOMPAT_H
#define DFCOMPAT_H

#include <sys/types.h>

#ifndef __DECONST
#define __DECONST(type, var)    ((type)(uintptr_t)(const void *)(var))
#endif

#ifdef NEED_STRLCPY
size_t strlcpy(char *, const char *, size_t);
#endif

#ifdef NEED_REALLOCF
void *reallocf(void *, size_t);
#endif

#ifdef NEED_GETPROGNAME
const char *getprogname(void);
#endif

#endif /* DFCOMPAT_H */
