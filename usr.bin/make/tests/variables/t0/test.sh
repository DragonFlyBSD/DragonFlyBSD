#!/bin/sh

# $DragonFly: src/usr.bin/make/tests/variables/t0/test.sh,v 1.1 2005/02/01 07:07:15 okumoto Exp $

. ../../env.sh

case $1 in
test)
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
	;;

compare) std_compare ;;
update) std_update ;;
run) std_run ;;
clean) std_clean ;;

*)
	echo "Usage: $0 run | compare | update | clean"
	;;
esac

