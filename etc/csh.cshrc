# $FreeBSD: src/etc/csh.cshrc,v 1.3 1999/08/27 23:23:40 peter Exp $
# $DragonFly: src/etc/csh.cshrc,v 1.3 2004/10/06 06:31:39 dillon Exp $
#
# System-wide .cshrc file for csh(1).

# a safer version of rm that isn't as annoying as -i
#
if ( $?prompt ) then
        alias rm 'rm -I'
endif
