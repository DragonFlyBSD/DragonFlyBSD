#!/bin/sh

# $DragonFly: src/usr.bin/make/tests/basic/t2/test.sh,v 1.6 2005/03/01 22:42:28 okumoto Exp $

. ../../common.sh

setup_test()
{
	cat > $WORK_BASE/Makefile << _EOF_
all:
	echo hello
_EOF_
}

desc_test()
{
	echo "A Makefile file with only a 'all:' file dependency specification, and shell command."
}

eval_cmd $1
