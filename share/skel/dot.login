# $FreeBSD: src/share/skel/dot.login,v 1.14.2.2 2001/08/01 17:24:32 obrien Exp $
# $DragonFly: src/share/skel/dot.login,v 1.2 2003/06/17 04:37:02 dillon Exp $
#
# .login - csh login script, read by login shell, after `.cshrc' at login.
#
# see also csh(1), environ(7).
#

[ -x /usr/games/fortune ] && /usr/games/fortune freebsd-tips
