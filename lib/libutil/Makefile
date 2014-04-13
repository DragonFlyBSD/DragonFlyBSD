#	@(#)Makefile	8.1 (Berkeley) 6/4/93
#	$FreeBSD: head/lib/libutil/Makefile 242381 2012-10-30 22:18:08Z bapt $

LIB=	util
SHLIB_MAJOR= 4
SHLIBDIR?= /lib
CFLAGS+=-DINET6
CFLAGS+=-D_CTYPE_H_DISABLE_MACROS_ 
SRCS=	flopen.c login.c login_tty.c logout.c logwtmp.c logwtmpx.c pty.c \
	login_cap.c login_class.c login_auth.c login_times.c login_ok.c \
	login_crypt.c loginx.c logoutx.c _secure_path.c uucplock.c \
	property.c auth.c \
	realhostname.c fparseln.c stub.c pidfile.c trimdomain.c \
	dehumanize_number.c humanize_number.c humanize_unsigned.c pw_util.c \
	efun.c getmntopts.c
SRCS+=	gr_util.c hexdump.c

INCS=	libutil.h login_cap.h
INCSLINKS=libutil.h ${INCLUDEDIR}/util.h
WARNS?=	3

CFLAGS+= -I${.CURDIR} -I${.CURDIR}/../libc/gen/

MAN+=	flopen.3 login.3 loginx.3 login_auth.3 login_tty.3 logout.3 logwtmp.3 \
	pty.3 \
	login_cap.3 login_class.3 login_times.3 login_ok.3 \
	_secure_path.3 uucplock.3 property.3 auth.3 realhostname.3 \
	realhostname_sa.3 trimdomain.3 fparseln.3 pidfile.3 \
	humanize_number.3 humanize_unsigned.3 \
	efun.3 getmntopts.3
MAN+=	login.conf.5 auth.conf.5
MAN+=	hexdump.3
MAN+=	pw_util.3

MLINKS+=auth.3 auth_getval.3
MLINKS+=efun.3 ecalloc.3 efun.3 emalloc.3 efun.3 erealloc.3 efun.3 esetfunc.3 \
	efun.3 estrdup.3 efun.3 estrlcat.3 efun.3 estrlcpy.3 \
	efun.3 estrndup.3 efun.3 easprintf.3
MLINKS+=humanize_number.3 dehumanize_number.3
MLINKS+=humanize_unsigned.3 format_bytes.3
MLINKS+=login_cap.3 login_getclassbyname.3 login_cap.3 login_close.3 \
	login_cap.3 login_getclass.3 login_cap.3 login_getuserclass.3 \
	login_cap.3 login_getcapstr.3 login_cap.3 login_getcaplist.3 \
	login_cap.3 login_getstyle.3 login_cap.3 login_getcaptime.3 \
	login_cap.3 login_getcapnum.3 login_cap.3 login_getcapsize.3 \
	login_cap.3 login_getcapbool.3 login_cap.3 login_getpath.3 \
	login_cap.3 login_getpwclass.3 login_cap.3 login_setcryptfmt.3
MLINKS+=loginx.3 logoutx.3 loginx.3 logwtmpx.3
MLINKS+=login_class.3 setusercontext.3 login_class.3 setclasscontext.3 \
	login_class.3 setclassenvironment.3 login_class.3 setclassresources.3
MLINKS+=login_times.3 parse_lt.3 login_times.3 in_ltm.3 \
	login_times.3 in_lt.3 login_times.3 in_ltms.3 \
	login_times.3 in_lts.3
MLINKS+=login_ok.3 auth_ttyok.3 login_ok.3 auth_hostok.3 \
	login_ok.3 auth_timeok.3
MLINKS+=login_auth.3 auth_checknologin.3 login_auth.3 auth_cat.3
MLINKS+=pidfile.3 pidfile_close.3 pidfile.3 pidfile_fileno.3 \
	pidfile.3 pidfile_open.3 pidfile.3 pidfile_remove.3 \
	pidfile.3 pidfile_write.3
MLINKS+=property.3 properties_read.3  property.3 properties_free.3
MLINKS+=property.3 property_find.3
MLINKS+=pty.3 openpty.3 pty.3 forkpty.3
MLINKS+=uucplock.3 uu_lock.3 uucplock.3 uu_lock_txfr.3 \
	uucplock.3 uu_unlock.3 uucplock.3 uu_lockerr.3
MLINKS+=pw_util.3 pw_copy.3 \
	pw_util.3 pw_dup.3 \
	pw_util.3 pw_edit.3 \
	pw_util.3 pw_equal.3 \
	pw_util.3 pw_fini.3 \
	pw_util.3 pw_init.3 \
	pw_util.3 pw_make.3 \
	pw_util.3 pw_make_v7.3 \
	pw_util.3 pw_mkdb.3 \
	pw_util.3 pw_lock.3 \
	pw_util.3 pw_scan.3 \
	pw_util.3 pw_tempname.3 \
	pw_util.3 pw_tmp.3

.include <bsd.lib.mk>
