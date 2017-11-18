# $FreeBSD: head/lib/libpam/static_libpam/Makefile 314901 2017-03-08 08:25:26Z brooks $

# Build only the static library here.
LIB=	pam
NOPIC=	yes

# Already handled by libpam shared version.
NOMAN=	noman
INCS=

#
# Static modules support
#
# We build static versions of all modules and of openpam_static.o,
# then link them all together into openpam_static_modules.o.  None of
# the modules export any symbols, but they store structures with
# pointers to their service functions in a linker set which the code
# in openpam_static.c traverses to locate the individual modules.
#
MODULESDIR=	../modules
MODULES=	pam_chroot \
		pam_deny \
		pam_echo \
		pam_exec \
		pam_ftpusers \
		pam_group \
		pam_guest \
		pam_lastlog \
		pam_login_access \
		pam_nologin \
		pam_opie \
		pam_opieaccess \
		pam_passwdqc \
		pam_permit \
		pam_radius \
		pam_rhosts \
		pam_rootok \
		pam_securetty \
		pam_self \
		pam_ssh \
		pam_tacplus \
		pam_unix

STATIC_MODULES=	${MODULES:C/.*/${MODULESDIR}\/&\/lib&.a/}
STATICOBJS+=	openpam_static_modules.o
CLEANFILES+=	openpam_static.o openpam_static.po \
		openpam_static_modules.o

openpam_static_modules.o: openpam_static.o ${STATIC_MODULES}
	${CC} -nostdlib ${LDFLAGS} -o ${.TARGET} -r -Wl,--whole-archive ${.ALLSRC}

openpam_static_modules.po: openpam_static.po ${STATIC_MODULES}
	${CC} -nostdlib ${LDFLAGS} -o ${.TARGET} -r -Wl,--whole-archive ${.ALLSRC}

.PATH: ${.CURDIR}/../libpam

.include "${.CURDIR}/../libpam/Makefile"
