#!/bin/sh

# $DragonFly: src/usr.bin/make/tests/basic/t1/test.sh,v 1.4 2005/02/25 12:28:13 okumoto Exp $

. ../../common.sh

run_test()
{
	cat > Makefile << _EOF_
all:
_EOF_
	$MAKE 1> stdout 2> stderr
	echo $? > status
}

eval_cmd $1
