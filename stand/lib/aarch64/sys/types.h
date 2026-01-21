#ifndef _SYS_TYPES_H_
#define _SYS_TYPES_H_

#include <machine/types.h>
#include <machine/stdint.h>

typedef __int8_t int8_t;
typedef __uint8_t u_int8_t;
typedef __int16_t int16_t;
typedef __uint16_t u_int16_t;
typedef __int32_t int32_t;
typedef __uint32_t u_int32_t;
typedef __int64_t int64_t;
typedef __uint64_t u_int64_t;

typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;
typedef unsigned long long u_longlong;

typedef __size_t size_t;
typedef __ssize_t ssize_t;
typedef __off_t off_t;
typedef __pid_t pid_t;
typedef __clock_t clock_t;
typedef __clockid_t clockid_t;
typedef __time_t time_t;
typedef __timer_t timer_t;
typedef __rlim_t rlim_t;
typedef __suseconds_t suseconds_t;
typedef __register_t register_t;

typedef u_int32_t uid_t;
typedef u_int32_t gid_t;
typedef u_int32_t mode_t;
typedef u_int32_t nlink_t;
typedef u_int64_t ino_t;
typedef u_int32_t dev_t;

#endif
