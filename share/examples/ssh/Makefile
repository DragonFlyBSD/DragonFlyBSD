.PATH: ${.CURDIR}/../../../crypto/openssh

NOOBJ=	noobj
FILES=		moduli ssh_config sshd_config
FILESDIR=	/usr/share/examples/etc/ssh
FILESOWN=	${BINOWN}
FILESGRP=	${BINGRP}
FILESMODE=	644

# do not treat these files as executables
moduli:
	@true

ssh_config:
	@true

sshd_config:
	@true

.include <bsd.prog.mk>
