PROG=	ssh-pkcs11-helper
SRCS=	ssh-pkcs11-helper.c roaming_dummy.c
MAN=	ssh-pkcs11-helper.8

DPADD+=	${LIBZ}
LDADD+=	-lz

.include "../../Makefile.ssh.common"
.include <bsd.prog.mk>
