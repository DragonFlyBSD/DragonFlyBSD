/*
 * Written by Alexander Kabaev <kan@FreeBSD.org>
 * The file is in public domain.
 *
 * $FreeBSD: head/lib/libc/secure/stack_protector_compat.c 286760 2015-08-14 03:03:13Z pfg $
 */

void __stack_chk_fail(void);

#ifdef PIC
void
__stack_chk_fail_local(void)
{

	__stack_chk_fail();
}
#endif
