/*
 *	$NetBSD: ealloc.h,v 1.1.1.1 1999/11/19 04:30:56 mrg Exp $
 *	$DragonFly: src/sbin/rcorder/ealloc.h,v 1.1 2003/07/24 06:35:38 dillon Exp $
 */

void	*emalloc __P((size_t len));
char	*estrdup __P((const char *str));
void	*erealloc __P((void *ptr, size_t size));
void	*ecalloc __P((size_t nmemb, size_t size));
