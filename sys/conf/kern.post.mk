# $DragonFly: src/sys/conf/kern.post.mk,v 1.1 2006/07/02 00:55:08 corecode Exp $
# 
# This Makefile covers the bottom part of the MI build instructions
#

.PHONY:	all modules

all: ${KERNEL}.stripped

depend: kernel-depend
clean:  kernel-clean
cleandepend:  kernel-cleandepend
tags:  kernel-tags
install: kernel-install
reinstall: kernel-reinstall

${KERNEL}.stripped: ${FULLKERNEL}
	${OBJCOPY} --strip-debug ${FULLKERNEL} ${KERNEL}.stripped

${FULLKERNEL}: ${SYSTEM_DEP} vers.o
	@rm -f ${.TARGET}
	@echo linking ${.TARGET}
	${SYSTEM_LD}
	${SYSTEM_LD_TAIL}

.if !exists(.depend)
${SYSTEM_OBJS}: ${BEFORE_DEPEND:M*.h} ${MFILES:T:S/.m$/.h/}
.endif

.for mfile in ${MFILES}
${mfile:T:S/.m$/.h/}: ${mfile}
	awk -f $S/tools/makeobjops.awk -- -h ${mfile}
.endfor

kernel-clean:
	rm -f *.o *.so *.So *.ko *.s eddep errs \
	      ${KERNEL} ${KERNEL}.debug ${KERNEL}.nodebug ${KERNEL}.stripped \
	      linterrs makelinks setdef[01].c setdefs.h tags \
	      vers.c vnode_if.c vnode_if.h \
	      ${MFILES:T:S/.m$/.c/} ${MFILES:T:S/.m$/.h/} \
	      ${CLEAN}

#lint: /tmp
#	@lint -hbxn -DGENERIC -Dvolatile= ${COPTS} \
#	  $S/$M/$M/Locore.c ${CFILES} ioconf.c | \
#	    grep -v 'struct/union .* never defined' | \
#	    grep -v 'possible pointer alignment problem'

locore.o: $S/$M/$M/locore.s assym.s
	${NORMAL_S}

# This is a hack.  BFD "optimizes" away dynamic mode if there are no
# dynamic references.  We could probably do a '-Bforcedynamic' mode like
# in the a.out ld.  For now, this works.
hack.So: Makefile
	touch hack.c
	${CC} -shared -nostdlib hack.c -o hack.So
	rm -f hack.c

# this rule stops ./assym.s in .depend from causing problems
./assym.s: assym.s

assym.s: $S/kern/genassym.sh genassym.o
	sh $S/kern/genassym.sh genassym.o > ${.TARGET}

genassym.o: $S/$M/$M/genassym.c
	${CC} -c ${CFLAGS:N-fno-common} $S/$M/$M/genassym.c

${SYSTEM_OBJS} genassym.o vers.o: opt_global.h

# The argument list can be very long, use make -V and xargs to
# pass it to mkdep.
kernel-depend: assym.s ${BEFORE_DEPEND} \
	    ${CFILES} ${SYSTEM_CFILES} ${GEN_CFILES} ${SFILES} \
	    ${SYSTEM_SFILES} ${MFILES:T:S/.m$/.h/}
	rm -f .newdep
	${MAKE} -V CFILES -V SYSTEM_CFILES -V GEN_CFILES | xargs \
		mkdep -a -f .newdep ${CFLAGS}
	${MAKE} -V SFILES -V SYSTEM_SFILES | xargs \
	    env MKDEP_CPP="${CC} -E" mkdep -a -f .newdep ${ASM_CFLAGS}
	rm -f .depend
	mv -f .newdep .depend

kernel-cleandepend:
	rm -f .depend

links:
	egrep '#if' ${CFILES} | sed -f $S/conf/defines | \
	  sed -e 's/:.*//' -e 's/\.c/.o/' | sort -u > dontlink
	${MAKE} -V CFILES | tr -s ' ' '\12' | sed 's/\.c/.o/' | \
	  sort -u | comm -23 - dontlink | \
	  sed 's,../.*/\(.*.o\),rm -f \1;ln -s ../GENERIC/\1 \1,' > makelinks
	sh makelinks && rm -f dontlink

kernel-tags:
	@[ -f .depend ] || { echo "you must make depend first"; exit 1; }
	sh $S/conf/systags.sh
	rm -f tags1
	sed -e 's,      ../,    ,' tags > tags1

# Note: when moving the existing kernel to .old, make sure it is stripped
# so we do not have two full debug environments sitting in / eating up space.
#
kernel-install:
	@if [ ! -f ${SELECTEDKERNEL} ] ; then \
		echo "You must build a kernel first." ; \
		exit 1 ; \
	fi
.ifdef NOFSCHG
.if exists(${DESTDIR}/${KERNEL})
	${OBJCOPY} --strip-debug ${DESTDIR}/${KERNEL} ${DESTDIR}/${KERNEL}.old
.endif
	${INSTALL} -m 555 -o root -g wheel \
		${SELECTEDKERNEL} ${DESTDIR}/${KERNEL}
.else
.if exists(${DESTDIR}/${KERNEL})
	-chflags noschg ${DESTDIR}/${KERNEL}
	${OBJCOPY} --strip-debug ${DESTDIR}/${KERNEL} ${DESTDIR}/${KERNEL}.old
.endif
	${INSTALL} -m 555 -o root -g wheel -fschg \
		${SELECTEDKERNEL} ${DESTDIR}/${KERNEL}
.endif

kernel-reinstall:
.ifdef NOFSCHG
	${INSTALL} -m 555 -o root -g wheel \
		${SELECTEDKERNEL} ${DESTDIR}/${KERNEL}
.else
	${INSTALL} -m 555 -o root -g wheel -fschg \
		${SELECTEDKERNEL} ${DESTDIR}/${KERNEL}
.endif

.if !defined(MODULES_WITH_WORLD) && !defined(NO_MODULES)
all:	modules
depend: modules-depend
clean:  modules-clean
cleandepend:  modules-cleandepend
cleandir:  modules-cleandir
tags:  modules-tags
install: modules-install
reinstall: modules-reinstall
.endif

modules:
	@mkdir -p ${.OBJDIR}
	cd $S ; env ${MKMODULESENV} ${MAKE} -f Makefile.modules obj ; \
	    env ${MKMODULESENV} ${MAKE} -f Makefile.modules all

modules-depend:
	@mkdir -p ${.OBJDIR}
	cd $S ; env ${MKMODULESENV} ${MAKE} -f Makefile.modules obj ; \
	    env ${MKMODULESENV} ${MAKE} -f Makefile.modules depend

modules-clean:
	cd $S ; env ${MKMODULESENV} ${MAKE} -f Makefile.modules clean

modules-cleandepend:
	cd $S ; env ${MKMODULESENV} ${MAKE} -f Makefile.modules cleandepend

# XXX huh?
#modules-clobber:	modules-clean
#	rm -rf ${MKMODULESENV}

modules-cleandir:
	cd $S ; env ${MKMODULESENV} ${MAKE} -f Makefile.modules cleandir

modules-tags:
	cd $S ; env ${MKMODULESENV} ${MAKE} -f Makefile.modules tags

# Note: when moving the existing modules to .old, make sure they are stripped
# so we do not have two full debug environments sitting in / eating up space.
#
modules-install:
.if !defined(NO_MODULES_OLD)
	set -- ${DESTDIR}/modules/*; \
	if [ -f "$$1" ]; then \
		mkdir -p ${DESTDIR}/modules.old; \
		for file; do \
		${OBJCOPY} --strip-debug $$file ${DESTDIR}/modules.old/$${file##*/}; \
		done; \
	fi
.endif
	cd $S ; env ${MKMODULESENV} ${MAKE} -f Makefile.modules install

modules-reinstall:
	cd $S ; env ${MKMODULESENV} ${MAKE} -f Makefile.modules install

config.o:
	${NORMAL_C}

ioconf.o:
	${NORMAL_C}

vers.c: $S/conf/newvers.sh $S/sys/param.h ${SYSTEM_DEP}
	sh $S/conf/newvers.sh ${KERN_IDENT} ${IDENT}

# XXX strictly, everything depends on Makefile because changes to ${PROF}
# only appear there, but we don't handle that.
vers.o:
	${NORMAL_C}

#vnode_if.c: $S/tools/vnode_if.awk $S/kern/vnode_if.src
#	awk -f $S/tools/vnode_if.awk -- -c $S/kern/vnode_if.src
#
#vnode_if.h: $S/tools/vnode_if.awk $S/kern/vnode_if.src
#	awk -f $S/tools/vnode_if.awk -- -h $S/kern/vnode_if.src
#
#vnode_if.o:
#	${NORMAL_C}

.include "$S/conf/bsd.kern.mk"
