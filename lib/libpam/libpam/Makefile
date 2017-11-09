
#
# Only build shared lib here, libpam.a is built by static_libpam.
# This is done to avoid circular dependencies for static modules.
#
.if !defined(LIB)
SHLIB_NAME=	libpam.so.${SHLIB_MAJOR}
.endif

SRCDIR=	${.CURDIR}/../../../contrib/openpam

MODULE_DIR=	${LIBDIR}/security

CFLAGS+= -DHAVE_CONFIG_H
CFLAGS+= -DOPENPAM_MODULES_DIR='"${MODULE_DIR}/"'
CFLAGS+= -I${SRCDIR}/lib -I${SRCDIR}/include -I${.CURDIR}/../libpam

WARNS=	3

# Dist files
SRCS=	\
	openpam_borrow_cred.c \
	openpam_check_owner_perms.c \
	openpam_configure.c \
	openpam_constants.c \
	openpam_dispatch.c \
	openpam_dynamic.c \
	openpam_features.c \
	openpam_findenv.c \
	openpam_free_data.c \
	openpam_free_envlist.c \
	openpam_get_feature.c \
	openpam_get_option.c \
	openpam_load.c \
	openpam_log.c \
	openpam_nullconv.c \
	openpam_readline.c \
	openpam_readlinev.c \
	openpam_readword.c \
	openpam_restore_cred.c \
	openpam_set_feature.c \
	openpam_set_option.c \
	openpam_straddch.c \
	openpam_subst.c \
	openpam_ttyconv.c \
	pam_acct_mgmt.c \
	pam_authenticate.c \
	pam_chauthtok.c \
	pam_close_session.c \
	pam_end.c \
	pam_error.c \
	pam_get_authtok.c \
	pam_get_data.c \
	pam_get_item.c \
	pam_get_user.c \
	pam_getenv.c \
	pam_getenvlist.c \
	pam_info.c \
	pam_open_session.c \
	pam_prompt.c \
	pam_putenv.c \
	pam_set_data.c \
	pam_set_item.c \
	pam_setcred.c \
	pam_setenv.c \
	pam_start.c \
	pam_strerror.c \
	pam_verror.c \
	pam_vinfo.c \
	pam_vprompt.c
# Local files
SRCS+=	pam_debug_log.c

PAMINCS=openpam.h \
	openpam_attr.h \
	openpam_version.h \
	pam_appl.h \
	pam_constants.h \
	pam_modules.h \
	pam_types.h
INCS?=	${PAMINCS:S;^;${SRCDIR}/include/security/;} security/pam_mod_misc.h
INCSDIR=	${INCLUDEDIR}/security

MAN?=	openpam.3 \
	openpam_borrow_cred.3 \
	openpam_free_data.3 \
	openpam_free_envlist.3 \
	openpam_get_feature.3 \
	openpam_get_option.3 \
	openpam_log.3 \
	openpam_nullconv.3 \
	openpam_readline.3 \
	openpam_readlinev.3 \
	openpam_readword.3 \
	openpam_restore_cred.3 \
	openpam_set_feature.3 \
	openpam_set_option.3 \
	openpam_straddch.3 \
	openpam_subst.3 \
	openpam_ttyconv.3 \
	pam.3 \
	pam_acct_mgmt.3 \
	pam_authenticate.3 \
	pam_chauthtok.3 \
	pam_close_session.3 \
	pam_conv.3 \
	pam_end.3 \
	pam_error.3 \
	pam_get_authtok.3 \
	pam_get_data.3 \
	pam_get_item.3 \
	pam_get_user.3 \
	pam_getenv.3 \
	pam_getenvlist.3 \
	pam_info.3 \
	pam_open_session.3 \
	pam_prompt.3 \
	pam_putenv.3 \
	pam_set_data.3 \
	pam_set_item.3 \
	pam_setcred.3 \
	pam_setenv.3 \
	pam_sm_acct_mgmt.3 \
	pam_sm_authenticate.3 \
	pam_sm_chauthtok.3 \
	pam_sm_close_session.3 \
	pam_sm_open_session.3 \
	pam_sm_setcred.3 \
	pam_start.3 \
	pam_strerror.3 \
	pam_verror.3 \
	pam_vinfo.3 \
	pam_vprompt.3 \
	pam.conf.5

.PATH: ${SRCDIR}/lib ${SRCDIR}/doc/man

.include <bsd.lib.mk>
