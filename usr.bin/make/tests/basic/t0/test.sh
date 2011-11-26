#!/bin/sh

# $DragonFly: src/usr.bin/make/tests/basic/t0/test.sh,v 1.6 2005/03/01 22:42:28 okumoto Exp $

. ../../common.sh

setup_test()
{
	cp /dev/null $WORK_BASE/Makefile
}

desc_test()
{
	echo "A empty Makefile file."
}

eval_cmd $1
