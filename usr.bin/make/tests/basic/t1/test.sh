#!/bin/sh

# $DragonFly: src/usr.bin/make/tests/basic/t1/test.sh,v 1.5 2005/02/26 10:43:29 okumoto Exp $

. ../../common.sh

run_test()
{
	cat > Makefile << _EOF_
all:
_EOF_

	$MAKE 1> stdout 2> stderr
	echo $? > status
}

desc_test()
{
	echo "A Makefile file with only a 'all:' file dependency specification."
}

eval_cmd $1
