#!/bin/sh

# $DragonFly: src/usr.bin/make/tests/basic/t3/test.sh,v 1.1 2005/02/26 10:43:09 okumoto Exp $

. ../../common.sh

run_test()
{
	rm -f Makefile

	$MAKE 1> stdout 2> stderr
	echo $? > status
}

desc_test()
{
	echo "No Makefile file."
}

eval_cmd $1
