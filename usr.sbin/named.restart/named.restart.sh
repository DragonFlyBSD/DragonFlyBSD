#!/bin/sh -
#
# $DragonFly: src/usr.sbin/named.restart/named.restart.sh,v 1.4 2004/05/31 17:52:30 dillon Exp $
#
# RNDC does not support 'restart' yet, so do it using RCNG.

/sbin/rcrestart named

