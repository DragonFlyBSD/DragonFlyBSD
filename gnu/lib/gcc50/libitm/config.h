/* config.h.  Generated from config.h.in by configure.  */
/* config.h.in.  Generated from configure.ac by autoheader.  */

/* Define if building universal (internal helper macro) */
/* #undef AC_APPLE_UNIVERSAL_BUILD */

/* Define to 1 if the target supports 64-bit __sync_*_compare_and_swap */
#define HAVE_64BIT_SYNC_BUILTINS 1

/* Define to 1 if the assembler supports AVX. */
#define HAVE_AS_AVX 1

/* Define if your assembler supports .cfi_* directives. */
#define HAVE_AS_CFI_PSEUDO_OP 1

/* Define to 1 if the assembler supports HTM. */
/* #undef HAVE_AS_HTM */

/* Define to 1 if the assembler supports RTM. */
#define HAVE_AS_RTM 1

/* Define to 1 if the target supports __attribute__((alias(...))). */
#define HAVE_ATTRIBUTE_ALIAS 1

/* Define to 1 if the target supports __attribute__((dllexport)). */
/* #undef HAVE_ATTRIBUTE_DLLEXPORT */

/* Define to 1 if the target supports __attribute__((visibility(...))). */
#define HAVE_ATTRIBUTE_VISIBILITY 1

/* Define if the POSIX Semaphores do not work on your system. */
/* #undef HAVE_BROKEN_POSIX_SEMAPHORES */

/* Define to 1 if the target assembler supports thread-local storage. */
/* #undef HAVE_CC_TLS */

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if target has a weakref that works like the ELF one. */
#define HAVE_ELF_STYLE_WEAKREF 1

/* Define to 1 if you have the `getauxval' function. */
/* #undef HAVE_GETAUXVAL */

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the <malloc.h> header file. */
#define HAVE_MALLOC_H 1

/* Define to 1 if you have the `memalign' function. */
/* #undef HAVE_MEMALIGN */

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define if mmap with MAP_ANON(YMOUS) works. */
#define HAVE_MMAP_ANON 1

/* Define if mmap of /dev/zero works. */
#define HAVE_MMAP_DEV_ZERO 1

/* Define if read-only mmap of a plain file works. */
#define HAVE_MMAP_FILE 1

/* Define to 1 if you have the `posix_memalign' function. */
#define HAVE_POSIX_MEMALIGN 1

/* Define to 1 if you have the <semaphore.h> header file. */
#define HAVE_SEMAPHORE_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the `strtoull' function. */
#define HAVE_STRTOULL 1

/* Define to 1 if the target supports __sync_*_compare_and_swap */
#define HAVE_SYNC_BUILTINS 1

/* Define to 1 if you have the <sys/auxv.h> header file. */
/* #undef HAVE_SYS_AUXV_H */

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/time.h> header file. */
#define HAVE_SYS_TIME_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if the target supports thread-local storage. */
#define HAVE_TLS 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if GNU symbol versioning is used for libitm. */
#define LIBITM_GNU_SYMBOL_VERSIONING 1

/* Define to the sub-directory in which libtool stores uninstalled libraries.
   */
#define LT_OBJDIR ".libs/"

/* Define to the letter to which size_t is mangled. */
#define MANGLE_SIZE_T m

/* Name of package */
#define PACKAGE "libitm"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT ""

/* Define to the full name of this package. */
#define PACKAGE_NAME "GNU TM Runtime Library"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "GNU TM Runtime Library 1.0"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "libitm"

/* Define to the home page for this package. */
#define PACKAGE_URL "http://www.gnu.org/software/libitm/"

/* Define to the version of this package. */
#define PACKAGE_VERSION "1.0"

/* The size of `char', as computed by sizeof. */
/* #undef SIZEOF_CHAR */

/* The size of `int', as computed by sizeof. */
/* #undef SIZEOF_INT */

/* The size of `long', as computed by sizeof. */
/* #undef SIZEOF_LONG */

/* The size of `short', as computed by sizeof. */
/* #undef SIZEOF_SHORT */

/* The size of `void *', as computed by sizeof. */
/* #undef SIZEOF_VOID_P */

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Define if you can safely include both <string.h> and <strings.h>. */
#define STRING_WITH_STRINGS 1

/* Define to 1 if you can safely include both <sys/time.h> and <time.h>. */
#define TIME_WITH_SYS_TIME 1

/* Version number of package */
#define VERSION "1.0"

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

#ifndef WORDS_BIGENDIAN
#define WORDS_BIGENDIAN 0
#endif
