#!/bin/sh

# $DragonFly: src/usr.bin/make/tests/basic/t1/test.sh,v 1.6 2005/03/01 22:42:28 okumoto Exp $

. ../../common.sh

setup_test()
{
	cat > $WORK_BASE/Makefile << _EOF_
all:
_EOF_
}

desc_test()
{
	echo "A Makefile file with only a 'all:' file dependency specification."
}

eval_cmd $1
