/*
 *	$NetBSD: ealloc.h,v 1.1.1.1 1999/11/19 04:30:56 mrg Exp $
 *	$DragonFly: src/sbin/rcorder/ealloc.h,v 1.2 2003/11/01 17:16:01 drhodus Exp $
 */

void	*emalloc(size_t len);
char	*estrdup(const char *str);
void	*erealloc(void *ptr, size_t size);
void	*ecalloc(size_t nmemb, size_t size);
