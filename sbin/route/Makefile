#	@(#)Makefile	8.1 (Berkeley) 6/5/93
# $FreeBSD: src/sbin/route/Makefile,v 1.11.2.3 2002/12/04 14:05:42 ru Exp $
# $DragonFly: src/sbin/route/Makefile,v 1.5 2005/03/16 04:18:35 cpressey Exp $

PROG=	route
WARNS?=	6
MAN=	route.8
SRCS=	route.c show.c keywords.h keywords.c
CFLAGS+=-I. -DNS
CFLAGS+=-DINET6
CLEANFILES+=keywords.h keywords.c _keywords.tmp

keywords.h:
	echo -e "extern struct keytab {\n\tconst char\t*kt_cp;\n\tint\t\t kt_i;\n} keywords[];\n\n" > ${.TARGET}
	sed -e '/^#/d' -e '/^$$/d' ${.CURDIR}/keywords > _keywords.tmp
	tr a-z A-Z < _keywords.tmp | paste _keywords.tmp - | \
	    awk '{ \
		if (NF > 1) \
			printf "#define\tK_%s\t%d\n", $$2, NR }' \
	    >> ${.TARGET}
	rm -f _keywords.tmp

keywords.c:	keywords.h
	echo -e "#include \"keywords.h\"\n\nstruct keytab keywords[] = {\n\n" \
	    > ${.TARGET}
	sed -e '/^#/d' -e '/^$$/d' ${.CURDIR}/keywords > _keywords.tmp
	tr a-z A-Z < _keywords.tmp | paste _keywords.tmp - | \
	    awk '{ \
		if (NF > 1) \
			printf "\t{\"%s\", K_%s},\n", $$1, $$2 }' \
	    >> ${.TARGET}
	echo -e "\t{0, 0}\n};\n" >> ${.TARGET}
	rm -f _keywords.tmp

.include <bsd.prog.mk>
