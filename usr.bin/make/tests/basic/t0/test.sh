#!/bin/sh

# $DragonFly: src/usr.bin/make/tests/basic/t0/test.sh,v 1.5 2005/02/26 10:43:29 okumoto Exp $

. ../../common.sh

run_test()
{
	cp /dev/null Makefile

	$MAKE 1> stdout 2> stderr
	echo $? > status
}

desc_test()
{
	echo "A empty Makefile file."
}

eval_cmd $1
