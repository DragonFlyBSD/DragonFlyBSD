#!/bin/sh

# $DragonFly: src/usr.bin/make/tests/basic/t2/test.sh,v 1.5 2005/02/26 10:43:29 okumoto Exp $

. ../../common.sh

run_test()
{
	cat > Makefile << _EOF_
all:
	echo hello
_EOF_
	$MAKE 1> stdout 2> stderr
	echo $? > status
}

desc_test()
{
	echo "A Makefile file with only a 'all:' file dependency specification, and shell command."
}

eval_cmd $1
