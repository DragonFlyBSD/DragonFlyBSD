# $FreeBSD: src/sys/tools/emu10k1-mkalsa.sh,v 1.1 2004/02/05 22:51:16 peter Exp $
# $DragonFly: src/sys/tools/emu10k1-mkalsa.sh,v 1.1 2007/01/04 21:47:03 corecode Exp $

GREP=${GREP:-grep}
CC=${CC:-cc}
AWK=${AWK:-awk}
MV=${MV:=mv}
RM=${RM:=rm}
IN=$1
OUT=$2

trap "${RM} -f $OUT.tmp" EXIT

$GREP -v '#include' $IN | \
$CC -E -D__KERNEL__ -dM -  | \
$AWK -F"[     (]" '
/define/  {
	print "#ifndef " $2;
	print;
	print "#endif";
}' > $OUT.tmp
${MV} -f $OUT.tmp $OUT
