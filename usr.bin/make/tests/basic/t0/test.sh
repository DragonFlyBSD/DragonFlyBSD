#!/bin/sh

# $DragonFly: src/usr.bin/make/tests/basic/t0/test.sh,v 1.2 2005/02/01 11:19:37 okumoto Exp $

. ../../env.sh

case $1 in
test)
	cat > Makefile << _EOF_
_EOF_
	$MAKE 1> stdout 2> stderr
	echo $? > status
	;;

compare) std_compare ;;
diff) std_diff ;;
update)	std_update ;;
run) std_run ;;
clean) std_clean ;;

*)
	echo "Usage: $0 run | compare | update | clean"
	;;
esac

