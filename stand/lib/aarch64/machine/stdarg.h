#ifndef _MACHINE_STDARG_H_
#define _MACHINE_STDARG_H_

typedef __builtin_va_list __va_list;

#define __va_start(ap, last) __builtin_va_start(ap, last)
#define __va_end(ap) __builtin_va_end(ap)
#define __va_arg(ap, type) __builtin_va_arg(ap, type)

#endif
