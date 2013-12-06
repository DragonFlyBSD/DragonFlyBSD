/* config.h.  Generated from config.in by configure.  */
/* config.in.  Generated from configure.ac by autoheader.  */

/* Check that config.h is #included before system headers
   (this works only for glibc, but that should be enough).  */
#if defined(__GLIBC__) && !defined(__FreeBSD_kernel__) && !defined(__CONFIG_H__)
#  error config.h must be #included before system headers
#endif
#define __CONFIG_H__ 1

/* Define if building universal (internal helper macro) */
/* #undef AC_APPLE_UNIVERSAL_BUILD */

/* Define to 1 if translation of program messages to the user's native
   language is requested. */
/* #undef ENABLE_NLS */

/* Define to enable linker plugins */
/* #undef ENABLE_PLUGINS */

/* Define to do multi-threaded linking */
/* #undef ENABLE_THREADS */

/* Default big endian (true or false) */
#define GOLD_DEFAULT_BIG_ENDIAN false

/* Default machine code */
/* #define GOLD_DEFAULT_MACHINE EM_X86_64 */

/* Default OSABI code */
#define GOLD_DEFAULT_OSABI ELFOSABI_NONE

/* Default size (32 or 64) */
/* #define GOLD_DEFAULT_SIZE 64 */

/* Define to 1 if you have the <byteswap.h> header file. */
/* #undef HAVE_BYTESWAP_H */

/* Define to 1 if you have the `chsize' function. */
/* #undef HAVE_CHSIZE */

/* Define to 1 if you have the declaration of `asprintf', and to 0 if you
   don't. */
#define HAVE_DECL_ASPRINTF 1

/* Define to 1 if you have the declaration of `basename', and to 0 if you
   don't. */
#define HAVE_DECL_BASENAME 0

/* Define to 1 if you have the declaration of `ffs', and to 0 if you don't. */
#define HAVE_DECL_FFS 1

/* Define to 1 if you have the declaration of `memmem', and to 0 if you don't.
   */
#define HAVE_DECL_MEMMEM 1

/* Define to 1 if you have the declaration of `snprintf', and to 0 if you
   don't. */
#define HAVE_DECL_SNPRINTF 1

/* Define to 1 if you have the declaration of `strndup', and to 0 if you
   don't. */
#define HAVE_DECL_STRNDUP 1

/* Define to 1 if you have the declaration of `strverscmp', and to 0 if you
   don't. */
#define HAVE_DECL_STRVERSCMP 0

/* Define to 1 if you have the declaration of `vasprintf', and to 0 if you
   don't. */
#define HAVE_DECL_VASPRINTF 1

/* Define to 1 if you have the declaration of `vsnprintf', and to 0 if you
   don't. */
#define HAVE_DECL_VSNPRINTF 1

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you have the <ext/hash_map> header file. */
#define HAVE_EXT_HASH_MAP 1

/* Define to 1 if you have the <ext/hash_set> header file. */
#define HAVE_EXT_HASH_SET 1

/* Define to 1 if you have the `fallocate' function. */
/* #undef HAVE_FALLOCATE */

/* Define to 1 if you have the `ffsll' function. */
#define HAVE_FFSLL 1

/* Define to 1 if you have the `ftruncate' function. */
#define HAVE_FTRUNCATE 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define if your <locale.h> file defines LC_MESSAGES. */
#define HAVE_LC_MESSAGES 1

/* Define to 1 if you have the <locale.h> header file. */
#define HAVE_LOCALE_H 1

/* Define to 1 if you have the `mallinfo' function. */
/* #undef HAVE_MALLINFO */

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `mmap' function. */
#define HAVE_MMAP 1

/* Define to 1 if you have the mremap function with MREMAP_MAYMOVE support */
/* #undef HAVE_MREMAP */

/* Define if compiler supports #pragma omp threadprivate */
#define HAVE_OMP_SUPPORT 1

/* Define to 1 if you have the `posix_fallocate' function. */
/* #undef HAVE_POSIX_FALLOCATE */

/* Define to 1 if you have the `pread' function. */
#define HAVE_PREAD 1

/* Define to 1 if you have the `readv' function. */
#define HAVE_READV 1

/* Define to 1 if you have the `setlocale' function. */
#define HAVE_SETLOCALE 1

/* Define if struct stat has a field st_mtim with timespec for mtime */
#define HAVE_STAT_ST_MTIM 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the `sysconf' function. */
#define HAVE_SYSCONF 1

/* Define to 1 if you have the <sys/mman.h> header file. */
#define HAVE_SYS_MMAN_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to support 32-bit big-endian targets */
#define HAVE_TARGET_32_BIG 1

/* Define to support 32-bit little-endian targets */
#define HAVE_TARGET_32_LITTLE 1

/* Define to support 64-bit big-endian targets */
#define HAVE_TARGET_64_BIG 1

/* Define to support 64-bit little-endian targets */
#define HAVE_TARGET_64_LITTLE 1

/* Define if attributes work on C++ templates */
#define HAVE_TEMPLATE_ATTRIBUTES 1

/* Define to 1 if you have the `times' function. */
#define HAVE_TIMES 1

/* Define if std::tr1::hash<off_t> is usable */
#define HAVE_TR1_HASH_OFF_T 1

/* Define to 1 if you have the <tr1/unordered_map> header file. */
#define HAVE_TR1_UNORDERED_MAP 1

/* Define if ::std::tr1::unordered_map::rehash is usable */
#define HAVE_TR1_UNORDERED_MAP_REHASH 1

/* Define to 1 if you have the <tr1/unordered_set> header file. */
#define HAVE_TR1_UNORDERED_SET 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the <windows.h> header file. */
/* #undef HAVE_WINDOWS_H */

/* Define to 1 if you have the <zlib.h> header file. */
#define HAVE_ZLIB_H 1

/* Default library search path */
#define LIB_PATH "::DEFAULT::"

/* Whether configured as a native linker */
#define NATIVE_LINKER 1

/* Name of package */
#define PACKAGE "gold"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT ""

/* Define to the full name of this package. */
#define PACKAGE_NAME "gold"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "gold 0.1"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "gold"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "0.1"

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* System root for target files */
#define TARGET_SYSTEM_ROOT ""

/* Whether the system root can be relocated */
#define TARGET_SYSTEM_ROOT_RELOCATABLE 0

/* Enable extensions on AIX 3, Interix.  */
#ifndef _ALL_SOURCE
# define _ALL_SOURCE 1
#endif
/* Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif
/* Enable threading extensions on Solaris.  */
#ifndef _POSIX_PTHREAD_SEMANTICS
# define _POSIX_PTHREAD_SEMANTICS 1
#endif
/* Enable extensions on HP NonStop.  */
#ifndef _TANDEM_SOURCE
# define _TANDEM_SOURCE 1
#endif
/* Enable general extensions on Solaris.  */
#ifndef __EXTENSIONS__
# define __EXTENSIONS__ 1
#endif


/* Version number of package */
#define VERSION "0.1"

/* Define WORDS_BIGENDIAN to 1 if your processor stores words with the most
   significant byte first (like Motorola and SPARC, unlike Intel). */
#if defined AC_APPLE_UNIVERSAL_BUILD
# if defined __BIG_ENDIAN__
#  define WORDS_BIGENDIAN 1
# endif
#else
# ifndef WORDS_BIGENDIAN
/* #  undef WORDS_BIGENDIAN */
# endif
#endif

/* Define to 1 if on MINIX. */
/* #undef _MINIX */

/* Define to 2 if the system does not provide POSIX.1 features except with
   this defined. */
/* #undef _POSIX_1_SOURCE */

/* Define to 1 if you need to in order for `stat' and other things to work. */
/* #undef _POSIX_SOURCE */
