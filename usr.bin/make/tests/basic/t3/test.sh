#!/bin/sh

# $DragonFly: src/usr.bin/make/tests/basic/t3/test.sh,v 1.2 2005/03/01 22:42:28 okumoto Exp $

. ../../common.sh

setup_test()
{
	rm -f $WORK_BASE/Makefile
}

desc_test()
{
	echo "No Makefile file."
}

eval_cmd $1
