#ifndef _STDDEF_H_
#define _STDDEF_H_

#include <sys/types.h>

#ifndef PTRDIFF_T
typedef __intlp_t ptrdiff_t;
#define PTRDIFF_T
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef offsetof
#define offsetof(type, member)	__builtin_offsetof(type, member)
#endif

#endif
