#!/bin/sh

# $DragonFly: src/usr.bin/make/tests/basic/t0/test.sh,v 1.3 2005/02/25 11:57:32 okumoto Exp $

. ../../env.sh

run_test()
{
	cat > Makefile << _EOF_
_EOF_
	$MAKE 1> stdout 2> stderr
	echo $? > status
}

eval_cmd $1
