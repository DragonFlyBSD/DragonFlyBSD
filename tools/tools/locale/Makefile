# See https://wiki.freebsd.org/LocaleNewApproach
# Taken from FreeBSD svn [base]/user/edwin/locale/cldr
#
# needs:
# devel/p5-Tie-IxHash
#
# Modified by John Marino to suit DragonFly needs
#

.if !defined(CLDRDIR)
CLDRDIR!=	grep ^cldr etc/unicode.conf | cut -f 2 -d " "
.endif
.if !defined(UNIDATADIR)
UNIDATADIR!=	grep ^unidata etc/unicode.conf | cut -f 2 -d " "
.endif

ETCDIR=		${.CURDIR}/etc

TYPES?=		monetdef numericdef msgdef timedef
LOCALE_DESTDIR?=	/tmp/generated-locales/

.if defined(LC)
LC:=	--lc=${LC}
.endif

all:
.for t in ${TYPES}
	test -d ${t} || mkdir ${t}
	make build-${t}
.endfor
	@echo ""
	@find . -name *failed

install:
.for t in ${TYPES}
	cd ${t} && make
	cd ${t} && sudo DESTDIR=${LOCALE_DESTDIR} make install
.endfor

.for t in ${TYPES}
build-${t}:
	mkdir -p ${t} ${t}.draft
	perl -I tools tools/cldr2def.pl \
		--cldr=$$(realpath ${CLDRDIR}) \
		--unidata=$$(realpath ${UNIDATADIR}) \
		--etc=$$(realpath ${ETCDIR}) \
		--type=${t} ${LC}
	tools/finalize ${t}
.endfor

clean:
.for t in ${TYPES}
	rm -rf ${t} ${t}.draft
.endfor

