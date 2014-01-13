# 
# This Makefile covers the bottom part of the MI build instructions
#

.PHONY:	all modules

all: ${KERNEL}.stripped

_MACHINE_FWD=	${.OBJDIR}
.include "$S/conf/kern.fwd.mk"
.include "$S/conf/kern.paths.mk"

depend kernel-depend modules-depend: ${FORWARD_HEADERS_COOKIE}

depend: kernel-depend
clean:  kernel-clean
cleandepend:  kernel-cleandepend
tags:  kernel-tags
install: kernel-install
reinstall: kernel-reinstall

# Often developers just want the kernel, don't let
# -j builds leak into the modules until the kernel is done.
#
.ORDER: ${KERNEL}.stripped modules

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
	      linterrs setdef[01].c setdefs.h tags \
	      vers.c vnode_if.c vnode_if.h \
	      ${MFILES:T:S/.m$/.c/} ${MFILES:T:S/.m$/.h/} \
	      ${CLEAN}

#lint: /tmp
#	@lint -hbxn -DGENERIC -Dvolatile= ${COPTS} \
#	  $S/platform/$P/$M/Locore.c ${CFILES} ioconf.c | \
#	    grep -v 'struct/union .* never defined' | \
#	    grep -v 'possible pointer alignment problem'

locore.o: $S/platform/$P/$M/locore.s assym.s
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

genassym.o: $S/platform/$P/$M/genassym.c ${FORWARD_HEADERS_COOKIE} \
	    ${MFILES:T:S/.m$/.h/}
	${CC} -c ${CFLAGS:N-fno-common:N-mcmodel=small} ${WERROR} \
	$S/platform/$P/$M/genassym.c

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

kernel-tags:
	@[ -f .depend ] || { echo "you must make depend first"; /usr/bin/false; }
	sh $S/conf/systags.sh
	rm -f tags1
	sed -e 's,      ../,    ,' tags > tags1

# Note: when moving the existing kernel to .old, it is by default stripped
# so we do not have two full debug environments sitting in / eating up space.
#
# Also note the .old might be a file and not a directory, so we have to
# remove it first.
#
kernel-install: kernel-installable
	@if [ ! -f ${SELECTEDKERNEL} ]; then			\
		echo "You must build a kernel first.";		\
		/usr/bin/false;					\
	fi
	@if [ -f ${DESTDIR}${DESTKERNDIR}.old ]; then		\
		rm -f ${DESTDIR}${DESTKERNDIR}.old;		\
	fi
	mkdir -p ${DESTDIR}${DESTKERNDIR}.old
.if exists(${DESTDIR}${DESTKERNDIR}/${DESTKERNNAME})
.ifndef NOFSCHG
	-chflags noschg ${DESTDIR}${DESTKERNDIR}/${DESTKERNNAME}
.endif
.    ifdef NO_KERNEL_OLD_STRIP
	cp -p ${DESTDIR}${DESTKERNDIR}/${DESTKERNNAME} ${DESTDIR}${DESTKERNDIR}.old/${DESTKERNNAME}
.    else
	${OBJCOPY} --strip-debug ${DESTDIR}${DESTKERNDIR}/${DESTKERNNAME} ${DESTDIR}${DESTKERNDIR}.old/${DESTKERNNAME}
.    endif
.endif
	@if [ -f ${DESTDIR}${DESTKERNDIR} ]; then		\
		chflags noschg ${DESTDIR}${DESTKERNDIR};	\
		rm -f ${DESTDIR}${DESTKERNDIR};			\
	fi
	mkdir -p ${DESTDIR}${DESTKERNDIR}
.ifdef NOFSCHG
	${INSTALL} -m 555 -o root -g wheel \
		${SELECTEDKERNEL} ${DESTDIR}${DESTKERNDIR}/${DESTKERNNAME}
.else
	${INSTALL} -m 555 -o root -g wheel -fschg \
		${SELECTEDKERNEL} ${DESTDIR}${DESTKERNDIR}/${DESTKERNNAME}
.endif

kernel-reinstall: kernel-installable
	mkdir -p ${DESTDIR}${DESTKERNDIR}
.ifdef NOFSCHG
	${INSTALL} -m 555 -o root -g wheel \
		${SELECTEDKERNEL} ${DESTDIR}${DESTKERNDIR}/${DESTKERNNAME}
.else
	${INSTALL} -m 555 -o root -g wheel -fschg \
		${SELECTEDKERNEL} ${DESTDIR}${DESTKERNDIR}/${DESTKERNNAME}
.endif

kernel-installable:
	@if [ -f ${DESTDIR}/${DESTKERNNAME} ]; then \
		echo "You need to make buildworld, installworld, and upgrade"; \
		echo "before you can install a new kernel, because the"; \
		echo "kernel and modules have moved to /boot"; \
		/usr/bin/false; \
	fi
# Skip this step for vkernels
.if ${MACHINE_PLATFORM} != vkernel && ${MACHINE_PLATFORM} != vkernel64
	@if [ ! -f ${DESTDIR}/boot/dloader.rc ]; then \
		echo "You need to install a new ${DESTDIR}/boot before you"; \
		echo "can install a new kernel, kernels are now installed"; \
		echo "into a subdirectory along with their modules."; \
		echo "You can do this with a buildworld / installworld"; \
		echo "sequence."; \
		/usr/bin/false; \
	fi
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

# Note: when moving the existing modules to .old, they are by default stripped
# so we do not have two full debug environments sitting in / eating up space.
#
# We may have to remove deprecated kernel.old files before we can create
# the kernel.old directory.
#
modules-install: kernel-installable
.if !defined(NO_MODULES_OLD)
.  ifdef NO_KERNEL_OLD_STRIP
	set -- ${DESTDIR}${DESTKERNDIR}/*.ko;			\
	if [ -f "$$1" ]; then					\
		if [ -f ${DESTDIR}${DESTKERNDIR}.old ]; then	\
		    rm -f ${DESTDIR}${DESTKERNDIR}.old; 	\
		fi;						\
		mkdir -p ${DESTDIR}${DESTKERNDIR}.old;		\
		for file; do					\
		cp -p $$file ${DESTDIR}${DESTKERNDIR}.old;	\
		done;						\
	fi
.  else
	set -- ${DESTDIR}${DESTKERNDIR}/*.ko;			\
	if [ -f "$$1" ]; then					\
		if [ -f ${DESTDIR}${DESTKERNDIR}.old ]; then	\
		    rm -f ${DESTDIR}${DESTKERNDIR}.old; 	\
		fi;						\
		mkdir -p ${DESTDIR}${DESTKERNDIR}.old;		\
		for file; do					\
		${OBJCOPY} --strip-debug $$file ${DESTDIR}${DESTKERNDIR}.old/$${file##*/}; \
		done;						\
	fi
.  endif
	if [ -f ${DESTDIR}${DESTKERNDIR}/initrd.img ]; then	\
		cp -p ${DESTDIR}${DESTKERNDIR}/initrd.img ${DESTDIR}${DESTKERNDIR}.old; \
	fi
.endif
.if exists(${DESTDIR}/${OLDMODULESDIR})
	rm -rf ${DESTDIR}/${OLDMODULESDIR} # remove deprecated
.endif
	mkdir -p ${DESTDIR}${DESTKERNDIR} # Ensure that the modules directory exists!
	cd $S ; env ${MKMODULESENV} ${MAKE} -f Makefile.modules install

modules-reinstall:
	mkdir -p ${DESTDIR}/${DESTKERNDIR} # Ensure that the modules directory exists!
	cd $S ; env ${MKMODULESENV} ${MAKE} -f Makefile.modules install

config.o:
	${NORMAL_C} ${WERROR}

ioconf.o:
	${NORMAL_C} ${WERROR}

vers.c: $S/conf/newvers.sh $S/sys/param.h ${SYSTEM_DEP}
	sh $S/conf/newvers.sh $S/..

# XXX strictly, everything depends on Makefile because changes to ${PROF}
# only appear there, but we don't handle that.
vers.o:
	${NORMAL_C} ${WERROR}

.include "$S/conf/bsd.kern.mk"
