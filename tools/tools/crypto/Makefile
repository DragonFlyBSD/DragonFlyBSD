#	$FreeBSD: src/tools/tools/crypto/Makefile,v 1.2.2.2 2003/06/03 00:13:13 sam Exp $

ALL=	cryptotest cryptokeytest cryptostats \
	ubsecstats hifnstats ipsecstats

all:	${ALL}

# program to test symmetric crypto functions
cryptotest: cryptotest.c
	${CC} -o cryptotest cryptotest.c

# program to test asymmetric crypto functions
cryptokeytest: cryptokeytest.c
	${CC} -o cryptokeytest cryptokeytest.c -lcrypto

# program to dump statistics kept by the core crypto code
cryptostats: cryptostats.c
	${CC} -o cryptostats cryptostats.c

# program to print statistics kept by the Broadcom driver
ubsecstats: ubsecstats.c
	${CC} -o ubsecstats ubsecstats.c

# program to print statistics kept by the HIFN driver
hifnstats: hifnstats.c
	${CC} -o hifnstats hifnstats.c

# program to print statistics kept by fast ipsec
ipsecstats: ipsecstats.c
	${CC} -o ipsecstats ipsecstats.c

clean:
	rm -f ${ALL} core a.out
