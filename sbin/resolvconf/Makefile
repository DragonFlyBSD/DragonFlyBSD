DISTDIR=	${.CURDIR}/../../contrib/openresolv
.PATH:		${DISTDIR}

SCRIPTS=	resolvconf

FILES=		libc dnsmasq named pdnsd pdns_recursor unbound
FILESDIR=	/usr/libexec/resolvconf

MAN=		resolvconf.conf.5 resolvconf.8

CLEANFILES=	${SCRIPTS} ${FILES} ${MAN}

.for f in ${SCRIPTS} ${FILES} ${MAN}
${f}: ${f}.in
	sed	-e 's:@SBINDIR@:${BINDIR}:g' \
		-e 's:@SYSCONFDIR@:/etc:g' \
		-e 's:@RCDIR@:/etc/rc.d:g' \
		-e 's:@LIBEXECDIR@:${FILESDIR}:g' \
		-e 's:@VARDIR@:/var/run/resolvconf:g' \
		-e 's:@RESTARTCMD@::g' \
		-e 's:@STATUSARG@:onestatus:g' \
		${DISTDIR}/$@.in > $@
.endfor

# NOTE:
# The configuration file 'resolvconf.conf' is installed by 'etc/Makefile'

.include <bsd.prog.mk>
