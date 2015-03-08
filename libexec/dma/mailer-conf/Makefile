# This installs standard mailer.conf samples based on sendmail, postfix
# and OpenSMTPD from ports.  They are overwritten during each installworld

FILESDIR=	/etc/mail/
SHAREOWN=	root
SHAREGRP=	mail
FILESMODE=	640

FILES+=		README \
		mailer.conf.dma \
		mailer.conf.opensmtpd \
		mailer.conf.postfix \
		mailer.conf.sendmail

.include <bsd.prog.mk>
