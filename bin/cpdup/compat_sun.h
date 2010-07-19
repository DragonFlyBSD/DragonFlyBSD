/* __unused is a gcc'ism. */
#define	__unused

#include <stdio.h>
#include <stdarg.h>

int vasprintf(char **str, const char *format, va_list ap);
int asprintf(char **str, const char *format, ...);

#ifndef NOMD5

#include <md5.h>

char *MD5End(MD5_CTX *ctx, char *buf);
char *MD5File(const char *filename, char *buf);
char *MD5FileChunk(const char *filename, char *buf, off_t ofs, off_t len);

#endif
