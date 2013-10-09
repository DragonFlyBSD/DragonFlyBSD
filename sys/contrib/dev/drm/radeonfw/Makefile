# $FreeBSD: head/sys/contrib/dev/drm2/radeonkmsfw/Makefile 254885 2013-08-25 19:37:15Z dumbbell $

all:
	rm -f *.uu
	for file in *.bin; do \
	  uuencode -o $$file.uu $$file $$file; \
	  rm $$file; \
	done
