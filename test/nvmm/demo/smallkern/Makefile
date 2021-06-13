NAME=	smallkern
SRCS=	console.c main.c locore.S trap.S
LDSCRIPT= ${NAME}.ldscript

OBJDIR=	/tmp/${NAME}.obj
OBJS=	${SRCS:R:S/$/.o/g:S/^/${OBJDIR}\//g}
CFLAGS=	-g -std=c99 -Wall -Wextra -mno-red-zone -ffreestanding -D__x86_64__

all: /tmp/${NAME}

/tmp/${NAME}: ${OBJS} ${LDSCRIPT}
	ld -o ${.TARGET} \
		-X -z max-page-size=0x100000 -Ttext 0x100000 \
		-T ${.CURDIR}/${LDSCRIPT} ${OBJS}

.for src in ${SRCS}
${src:R:S/$/.o/g:S/^/${OBJDIR}\//g}: ${.CURDIR}/${src}
	@[ -d ${OBJDIR} ] || mkdir -p ${OBJDIR}
	cc ${CFLAGS} -o ${.TARGET} -c ${.ALLSRC}
.endfor

clean:
	rm -f /tmp/${NAME}
	rm -rf ${OBJDIR}
