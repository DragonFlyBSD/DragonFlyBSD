#ifndef _STRING_H_
#define _STRING_H_

#include <stddef.h>
#include <sys/cdefs.h>
#include <sys/types.h>


void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memchr(const void *s, int c, size_t n);
void bzero(void *b, size_t len);
void bcopy(const void *src, void *dst, size_t len);

char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);
size_t strlen(const char *s);
char *strdup(const char *s);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *s, const char *find);
char *strpbrk(const char *s, const char *accept);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
char *strtok(char *s, const char *delim);
char *strsep(char **stringp, const char *delim);
char *strerror(int errnum);

#endif
