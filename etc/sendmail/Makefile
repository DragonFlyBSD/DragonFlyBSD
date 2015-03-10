# This installs the sendmail mc templates and the makefile used to
# generate sendmail.cf and sendmail.submit.cf.  A README is also installed

FILESDIR=	/etc/mail/
FILESMODE=	644

FILES+=		Makefile \
		README \
		dragonfly.mc \
		dragonfly.submit.mc

CLEANFILES=	Makefile

Makefile: Makefile.4install
	cp ${.ALLSRC} ${.TARGET}

beforedepend:	Makefile

.include <bsd.prog.mk>
