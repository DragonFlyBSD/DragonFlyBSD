#!/bin/sh

# $DragonFly: src/usr.bin/make/tests/variables/t1/test.sh,v 1.3 2005/02/25 12:28:13 okumoto Exp $

. ../../common.sh

run_test()
{
	cat > Makefile << "_EOF_"
A	= 0
AV	= 1
all:
	echo $A
	echo ${AV}
	echo ${A}
	# The following are soo broken why no syntax error?
	echo $(
	echo $)
	echo ${
	echo ${A
	echo ${A)
	echo ${A){
	echo ${AV
	echo ${AV)
	echo ${AV){
	echo ${AV{
	echo ${A{
	echo $}
_EOF_
	$MAKE 1> stdout 2> stderr
	echo $? > status
}

eval_cmd $1
