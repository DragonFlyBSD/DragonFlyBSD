# $FreeBSD: src/sys/boot/ia64/skiload/skiload.cmd,v 1.1 2001/09/12 15:08:49 dfr Exp $
# $DragonFly: src/sys/boot/ia64/skiload/skiload.cmd,v 1.1 2003/11/10 06:08:37 dillon Exp $
iar
fr
pa
b enter_kernel
c
b printf
c
b rp
c
b ssc
c
b rp
c
bD
s 11
