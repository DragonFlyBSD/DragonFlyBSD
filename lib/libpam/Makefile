LIB=	pam

SRCDIR=	${.CURDIR}/../../contrib/openpam
CONTRIBDIR=	${SRCDIR}/lib
PATCHES!=	echo ${.CURDIR}/patches/*.patch

MODULE_DIR=	/usr/lib/security

CFLAGS+= -DOPENPAM_MODULES_DIR='"${MODULE_DIR}/"'
CFLAGS+= -DLIB_MAJ=${MODULE_SHLIB_MAJOR}
CFLAGS+= -I${SRCDIR}/lib

SRCS=	openpam_borrow_cred.c openpam_configure.c openpam_dispatch.c
SRCS+=	openpam_dynamic.c openpam_findenv.c openpam_free_data.c
SRCS+=	openpam_free_envlist.c openpam_get_option.c openpam_load.c
SRCS+=	openpam_log.c openpam_nullconv.c openpam_readline.c
SRCS+=	openpam_restore_cred.c openpam_set_option.c openpam_static.c
SRCS+=	openpam_ttyconv.c pam_acct_mgmt.c pam_authenticate.c
SRCS+=	pam_chauthtok.c pam_close_session.c pam_end.c pam_error.c
SRCS+=	pam_get_authtok.c pam_get_data.c pam_get_item.c pam_get_user.c
SRCS+=	pam_getenv.c pam_getenvlist.c pam_info.c pam_open_session.c
SRCS+=	pam_prompt.c pam_putenv.c pam_set_data.c pam_set_item.c pam_setcred.c
SRCS+=	pam_setenv.c pam_start.c pam_strerror.c pam_verror.c pam_vinfo.c
SRCS+=	pam_vprompt.c
SRCS+=	pam_debug_log.c
SRCS+=	${PATCHES}

PAMINCS=	openpam.h openpam_version.h pam_appl.h pam_constants.h
PAMINCS+=	pam_modules.h pam_types.h openpam_attr.h
INCS=	${PAMINCS:S;^;${SRCDIR}/include/security/;} pam_mod_misc.h
INCSDIR=	${INCLUDEDIR}/security

MAN=	openpam.3 openpam_borrow_cred.3 openpam_free_data.3
MAN+=	openpam_free_envlist.3 openpam_get_option.3 openpam_log.3
MAN+=	openpam_nullconv.3 openpam_readline.3 openpam_restore_cred.3
MAN+=	openpam_set_option.3 openpam_ttyconv.3 pam.3 pam_acct_mgmt.3
MAN+=	pam_authenticate.3 pam_chauthtok.3 pam_close_session.3 pam_conv.3
MAN+=	pam_end.3 pam_error.3 pam_get_authtok.3 pam_get_data.3 pam_get_item.3
MAN+=	pam_get_user.3 pam_getenv.3 pam_getenvlist.3 pam_info.3
MAN+=	pam_open_session.3 pam_prompt.3 pam_putenv.3 pam_set_data.3
MAN+=	pam_set_item.3 pam_setcred.3 pam_setenv.3 pam_sm_acct_mgmt.3
MAN+=	pam_sm_authenticate.3 pam_sm_chauthtok.3 pam_sm_close_session.3
MAN+=	pam_sm_open_session.3 pam_sm_setcred.3 pam_start.3 pam_strerror.3
MAN+=	pam_verror.3 pam_vinfo.3 pam_vprompt.3 pam.conf.5

.PATH: ${SRCDIR}/lib ${SRCDIR}/doc/man

.include "../pam_module/Makefile.shlib"
.include <bsd.lib.mk>
